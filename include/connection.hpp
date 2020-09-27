#pragma once

#include "asio.hpp"
#include <string>
#include <unordered_map>

namespace StratumExample {
enum class NetState
{
    Disconnected,
    Resolving,
    Connecting,
    Connected
};

class Connection
{
  public:
    Connection( const std::string&                                                 server,
                const std::string&                                                 port,
                const std::string&                                                 login,
                const std::string&                                                 pass,
                std::function<void( std::string )>                                 on_Set_Target_CB,
                std::function<void( std::string, bool, std::string, std::string )> on_Notify_CB );

    ~Connection();

  private:
    int                     m_CommandID;
    asio::io_context        m_IO_Context;
    asio::ip::tcp::socket   m_Socket;
    asio::ip::tcp::resolver m_Resolver;
    asio::executor_work_guard<asio::io_context::executor_type>
            m_Work_Guard;

    std::thread                          m_Worker_Thread;
    std::unordered_map<int, std::string> m_CommandMap;
    std::array<char, 65536>              m_Read_Buf;
    std::array<char, 65536>              m_Write_Buf;
    NetState                             m_State;

    std::function<void( std::string )>                                 m_On_Set_Target_CB;
    std::function<void( std::string, bool, std::string, std::string )> m_On_Notify_CB;

    std::string m_Server;
    std::string m_Port;
    std::string m_Login;
    std::string m_Pass;
    std::string m_Extra_Nonce;

    void resolve_callback( const asio::error_code&           err,
                           asio::ip::tcp::resolver::iterator endpoint_Iterator );

    void connect_callback( const asio::error_code& err );

    void write_callback( const asio::error_code& err, std::size_t bytes_Transferred );

    void read_callback( const asio::error_code& err, std::size_t bytes_Transferred );

    void parse_server_message( const std::string& raw_Message );
};
} // namespace Drill