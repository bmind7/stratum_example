#include "connection.hpp"
#include <iostream>

int
main( int argc, char const* argv[] )
{
    // Create connection to pool
    std::string                server = "cluster.aionpool.tech";
    std::string                port   = "2222";
    std::string                user   = "0xa0f41e6a0e098b324977e7334f91e69055ab6ca963ae8dcfa0bda08006518432.testworker";
    std::string                pass   = "x";
    StratumExample::Connection connection(
            server,
            port,
            user,
            pass,
            // On SetTarget callback
            []( std::string new_Target ) { std::cout << "New target - " << new_Target << std::endl; },
            // On Notify callback
            []( std::string job_ID, bool clean, std::string job_Target, std::string header_Hash ) { 
                                      std::cout << "New job: " << std::endl;
                                      std::cout << " - job_ID: " << job_ID << std::endl;
                                      std::cout << " - clean: " << clean << std::endl;
                                      std::cout << " - job_Target: " << job_Target << std::endl;
                                      std::cout << " - header_Hash: " << header_Hash << std::endl; } );

    std::cin.get();

    return 0;
}
