/**
 * @file namedserverfilter.cpp Namedserverfilter test
 *
 * Check that a readwritesplit service with a namedserverfilter will route a
 * SELECT @@server_id to the correct server. The filter is configured with
 * `match=SELECT` which should match any SELECT query.
 */


#include <iostream>
#include "testconnections.h"

using std::cout;
using IdSet = std::set<int>;

bool check_server_id(MYSQL* conn, IdSet& allowed_ids);

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->connect();
    int server_count = test.repl->N;
    if (server_count < 4)
    {
        test.expect(false, "Too few servers.");
        return test.global_result;
    }

    int server_ids[server_count];
    cout << "Server id:s are:";
    for (int i = 0; i < server_count; i++)
    {
        server_ids[i] = test.repl->get_server_id(i);
        cout << " " << server_ids[i];
    }
    cout << ".\n";

    auto maxconn = test.maxscales->open_rwsplit_connection(0);
    test.try_query(maxconn, "SELECT 1;");
    if (test.ok())
    {
        const char wrong_server[] = "Query went to wrong server.";
        IdSet allowed = {server_ids[1], server_ids[2]};
        // With all servers on, the query should go to either 2 or 3. Test several times.
        for (int i = 0; i < 5 && test.ok(); i++)
        {
            test.expect(check_server_id(maxconn, allowed), wrong_server);
        }

        if (test.ok())
        {
            int stopped_node = 1; // Stop server2
            test.repl->stop_node(stopped_node);
            test.maxscales->wait_for_monitor(1);
            allowed = {server_ids[2]};
            // Query should go to 3 only. Test several times.
            for (int i = 0; i < 5 && test.ok(); i++)
            {
                test.expect(check_server_id(maxconn, allowed), wrong_server);
            }
            test.repl->start_node(stopped_node, "");
        }
    }
    mysql_close(maxconn);

    test.repl->disconnect();
    return test.global_result;
}

bool check_server_id(MYSQL* conn, IdSet& allowed_ids)
{
    bool id_ok = false;
    char str[100];
    if (find_field(conn, "SELECT @@server_id", "@@server_id", str))
    {
        cout << "Failed to query for @@server_id: " << mysql_error(conn) << ".\n";
    }
    else
    {
        int queried_id = atoi(str);
        if (allowed_ids.count(queried_id))
        {
            id_ok = true;
        }
        else
        {
            cout << "Queried unexpected server id " << queried_id << ".\n";
        }
    }
    return id_ok;
}
