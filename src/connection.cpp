#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "connection.hpp"
#include "json.hpp"

namespace StratumExample {
//--------------------------------------------------------------------------
Connection::Connection( const std::string&                                                 server,
                        const std::string&                                                 port,
                        const std::string&                                                 login,
                        const std::string&                                                 pass,
                        std::function<void( std::string )>                                 on_Set_Target_CB,
                        std::function<void( std::string, bool, std::string, std::string )> on_Notify_CB ) :
    m_CommandID( 1 ),
    m_Socket( m_IO_Context ),
    m_Resolver( m_IO_Context ),
    m_Work_Guard( m_IO_Context.get_executor() ),
    m_Worker_Thread( [ & ]() { m_IO_Context.run(); } ),
    m_State( NetState::Disconnected ),
    m_On_Set_Target_CB( on_Set_Target_CB ),
    m_On_Notify_CB( on_Notify_CB ),
    m_Server( server ),
    m_Port( port ),
    m_Login( login ),
    m_Pass( pass )
{
    // Find the host
    m_State = NetState::Resolving;
    m_Resolver.async_resolve( m_Server,
                              m_Port,
                              std::bind( &Connection::resolve_callback, this, std::placeholders::_1, std::placeholders::_2 ) );
    // Signal that after current work loop is finished we can stop the context from execution
    m_Work_Guard.reset();
}
//--------------------------------------------------------------------------
Connection::~Connection()
{
    // End work of current context
    m_IO_Context.stop();
    m_Worker_Thread.join();
    std::cout << "Connection is closed" << std::endl;
}
//--------------------------------------------------------------------------
void
Connection::resolve_callback( const asio::error_code&           err,
                              asio::ip::tcp::resolver::iterator endpoint_Iterator )
{
    if ( err ) {
        m_State = NetState::Disconnected;
        std::cout << err.value() << ": " << err.message() << std::endl;
        return;
    }

    std::cout << "Host resolved: " << endpoint_Iterator->host_name() << std::endl;
    m_State = NetState::Connecting;
    m_Socket.async_connect( endpoint_Iterator->endpoint(),
                            std::bind( &Connection::connect_callback, this, std::placeholders::_1 ) );
}
//--------------------------------------------------------------------------
void
Connection::connect_callback( const asio::error_code& err )
{
    if ( err ) {
        m_State = NetState::Disconnected;
        std::cout << err.value() << ": " << err.message() << std::endl;
        return;
    }

    std::cout << "Connected" << std::endl;
    m_State = NetState::Connected;

    // Subscribe to server
    nlohmann::json message_json = {
        { "id", m_CommandID },
        { "method", "mining.subscribe" },
        { "params", { "MyMiner/1.0.0", nullptr, m_Server, m_Port } }
    };
    // By default JSON library doesn't append '\n' to the json line, do it manually
    std::ostringstream message;
    message << message_json.dump() << "\n";
    m_Socket.async_send( asio::buffer( message.str() ),
                         std::bind( &Connection::write_callback,
                                    this,
                                    std::placeholders::_1,
                                    std::placeholders::_2 ) );
    // Save cammand & ID pair to the commands map, do it in the asio thread
    // to avoid race conditions and manual synchronization
    asio::post( [ id = m_CommandID, this ] { m_CommandMap[ id ] = "mining.subscribe"; } );
    m_CommandID++;

    // Authorize on server
    message_json = { { "id", 2 }, { "method", "mining.authorize" }, { "params", { m_Login, m_Pass } } };
    message << message_json.dump() << "\n";
    m_Socket.async_send( asio::buffer( message.str() ),
                         std::bind( &Connection::write_callback,
                                    this,
                                    std::placeholders::_1,
                                    std::placeholders::_2 ) );
    // Save cammand & ID pair to the commands map, do it in the asio thread
    // to avoid race conditions and manual synchronization
    asio::post( [ id = m_CommandID, this ] { m_CommandMap[ id ] = "mining.authorize"; } );
    m_CommandID++;

    // Start monitoring all incoming messages from server
    m_Socket.async_receive( asio::buffer( m_Read_Buf ),
                            std::bind( &Connection::read_callback,
                                       this,
                                       std::placeholders::_1,
                                       std::placeholders::_2 ) );
}
//--------------------------------------------------------------------------
void
Connection::write_callback( const asio::error_code& err, std::size_t bytes_Transferred )
{
    if ( err ) {
        m_State = NetState::Disconnected;
        std::cout << err.value() << ": " << err.message() << std::endl;
        return;
    }
}
//--------------------------------------------------------------------------
void
Connection::read_callback( const asio::error_code& err, std::size_t bytes_Transferred )
{
    if ( err ) {
        m_State = NetState::Disconnected;
        std::cout << err.value() << ": " << err.message() << std::endl;
        return;
    }

    // Parsing messages from the data buffer
    for ( std::size_t start = 0, end = 0; end < bytes_Transferred; end++, start = end ) {
        // Skip until new line symbol is found
        while ( end < bytes_Transferred && m_Read_Buf[ end ] != '\n' ) {
            end++;
        }

        if ( end == bytes_Transferred ) {
            // There is possibility that we have an unfinished message,
            // in normal communication it should not appear
            // possible cause is misconfigured server which sends huge amount of data
            std::string unfinished_message( std::next( std::begin( m_Read_Buf ), start ),
                                            std::next( std::begin( m_Read_Buf ), end + 1 ) );
            std::cout << "[ERROR] unfinished data: " << unfinished_message << std::endl;

            // New line symbol was not found, exit message reading loop
            break;
        }

        if ( start == end ) {
            // Nothing to copy, move to next message
            continue;
        }

        // Copy selected sub array to the new message string
        std::string raw_message( std::next( std::begin( m_Read_Buf ), start ),
                                 std::next( std::begin( m_Read_Buf ), end ) );

        parse_server_message( raw_message );
    }

    if ( m_State == NetState::Disconnected ) {
        // exit the reading loop in case we lost connection
        return;
    }

    // Wait for the next incoming messages from server
    m_Socket.async_receive( asio::buffer( m_Read_Buf ),
                            std::bind( &Connection::read_callback,
                                       this,
                                       std::placeholders::_1,
                                       std::placeholders::_2 ) );
}
//--------------------------------------------------------------------------
void
Connection::parse_server_message( const std::string& raw_Message )
{
    //std::cout << "Raw message: " << raw_Message << std::endl;

    auto json_message = nlohmann::json::parse( raw_Message );

    if ( json_message[ "id" ] == nullptr &&
         json_message[ "method" ] == "mining.set_target" ) {
        std::cout << "New difficulty set to: " << json_message[ "params" ][ 0 ] << std::endl;
        m_On_Set_Target_CB( json_message[ "params" ][ 0 ] );
        return;
    }

    if ( json_message[ "id" ] == nullptr &&
         json_message[ "method" ] == "mining.notify" ) {
        std::cout << "Server notification: " << std::endl;
        std::cout << " - job id: " << json_message[ "params" ][ 0 ] << std::endl;
        std::cout << " - clean job: " << json_message[ "params" ][ 1 ] << std::endl;
        std::cout << " - job target: " << json_message[ "params" ][ 2 ] << std::endl;
        std::cout << " - header hash: " << json_message[ "params" ][ 3 ] << std::endl;
        m_On_Notify_CB( json_message[ "params" ][ 0 ],
                        json_message[ "params" ][ 1 ],
                        json_message[ "params" ][ 2 ],
                        json_message[ "params" ][ 3 ] );
        return;
    }

    if ( m_CommandMap.find( json_message[ "id" ] ) == m_CommandMap.end() ) {
        // Skip already processed response
        std::cout << "Command #" << json_message[ "id" ] << " was already processed" << std::endl;
        return;
    }

    if ( m_CommandMap[ json_message[ "id" ] ] == "mining.subscribe" ) {
        // Only save extra nonce
        m_Extra_Nonce = json_message[ "result" ][ 1 ];
        std::cout << "Extra nonce found: " << m_Extra_Nonce << std::endl;
    }

    if ( m_CommandMap[ json_message[ "id" ] ] == "mining.authorize" &&
         json_message[ "error" ] != nullptr ) {
        std::cout << "Authorization error: " << json_message[ "error" ] << std::endl;
        m_State = NetState::Disconnected;
    }

    if ( m_CommandMap[ json_message[ "id" ] ] == "mining.authorize" &&
         json_message[ "result" ] ) {
        std::cout << "Miner authorized" << std::endl;
    }

    m_CommandMap.erase( json_message[ "id" ] );
}
//--------------------------------------------------------------------------
} // namespace Drill