#include <iostream>
#include <utility>
#include <xmlrpc-c/client.hpp>
#include <unistd.h>
#include<vector>
#include <sstream>
#include <fstream>
#include <thread>
#include <xmlrpc-c/client_simple.hpp>
#include "latret.h"
#include <ev.h>
#include "conflicts.h"
#include "json.hpp"
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>


#include <xmlrpc-c/base.hpp>
#include <xmlrpc-c/registry.hpp>
#include <xmlrpc-c/server_abyss.hpp>
#include <cassert>
#include <stdexcept>
#include <iostream>

using namespace std;

enum TestType {TCP_STREAM, TCP_RR};
// a thread safe cout.
// stupid c++ doesnt have one.
class sc: public std::ostringstream
{
public:
    sc() = default;

    ~sc() override {
        std::lock_guard<std::mutex> guard(_mutexPrint);
        std::cout << this->str();
    }

private:
    static std::mutex _mutexPrint;
};

std::mutex sc::_mutexPrint{};

struct ClientState {
    int sockfd;
    long bytes, durationMillis;
    LatencyTimerThreadUnsafe rttLat;
};

struct NioForLoop;

struct ServerConnState {
    ev_io watcher;
    // some buffer
    string clientIp;
    long durationMillis=0;
    long bytes=0;
    NioForLoop *pNioForLoop;
    vector<char> buffer;
    int bytesReadSoFar=0;
    bool writing=false;
    bool writeInterest=false;
};


struct my_ev_prepare {
    ev_prepare prepare;
    NioForLoop* pNioForLoop;
};

struct my_ev_check {
    ev_check check;
    NioForLoop* pNioForLoop;
};

struct NioForLoop {
    struct my_ev_check check;
    struct my_ev_prepare prepare;
    struct ev_loop *loop;
    std::chrono::high_resolution_clock::time_point tp;
    vector<ServerConnState> connStates;
    LatencyTimerThreadUnsafe selectLat;
    LatencyTimerThreadUnsafe readLat;
    LatencyTimerThreadUnsafe writeLat;
    LatencyTimerThreadUnsafe totalLat;
};

struct IpPort {
    string ip;
    int port{};
    IpPort(string &ip, int port): ip(ip), port(port) {};
    IpPort() = default;
};


class NioPerfTest {
public:
    void go(unsigned long argc, const char* argv[]);
    vector<IpPort> parseIpPort(const string& str, int defaultPort);

    std::mutex serverInitMutex;
    std::mutex bigFatLock;
    std::condition_variable serverInitCv;
    int numClientsConnected = 0;
    int totClients = 0;
    vector<NioForLoop> nioForLoops;
    vector<thread> nioForLoopThreads;
    nlohmann::json testcaseObj; // the parsed current test case
    void serverInit(const string&testcase, int numClients);


    void serverListenCb(EV_P_ ev_io *w, int revents);
    void serverRunLoop(NioForLoop& nioForLoop);
    void server_tcp_stream_cb(EV_P_ ev_io *w, int revents);
    void server_tcp_rr_cb(EV_P_ ev_io *w, int revents);
    void serverPrepareCb(EV_P_ ev_prepare *w, int revents);
    void serverCheckCb(EV_P_ ev_check *w, int revents);
public:
    string type;
    IpPort serverRmi, serverReal;
    vector<IpPort> clientRmis;
    long timeMs = 10000;
    int gatewayRmiPort = 5000, serverRmiPort = 5001, clientRmiPort = 5002;
    string ipAddr = ""; //TODO
    void server();

    void client();
    vector<ClientState> clientStates;
    vector<thread> clientThreads;

    void clientInit(const string & testcase);
    void clientStart();
    void client_tcp_stream(ClientState& clientState);
    void client_tcp_rr(ClientState& clientState);
    string clientResult();
    void serverWaitForClientConnect();
    void serverStartNioLoops();
    string serverResult();
    void gateway();
};


// a trampoline
static void stdin_cb (EV_P_ ev_io *w, int revents)
{
    auto pobj = (NioPerfTest *)w->data;
    pobj->serverListenCb(loop, w, revents);
}

// a trampoline
static void tcp_stream_cb (EV_P_ ev_io *w, int revents)
{
    auto pobj = (NioPerfTest *)w->data;
    if(pobj==0L) {
        sc{}<<"wtfff in trampoline"<<endl;
    }
    pobj->server_tcp_stream_cb(loop, w, revents);
}

// a trampoline
static void tcp_rr_cb (EV_P_ ev_io *w, int revents)
{
    auto pobj = (NioPerfTest *)w->data;
    pobj->server_tcp_rr_cb(loop, w, revents);
}


// a trampoline
static void prepare_cb (EV_P_ ev_prepare *w, int revents)
{
    auto pobj = (NioPerfTest *)w->data;
    pobj->serverPrepareCb(loop, w, revents);
}

// a trampoline
static void check_cb (EV_P_ ev_check *w, int revents)
{
    auto pobj = (NioPerfTest *)w->data;
    pobj->serverCheckCb(loop, w, revents);
}


static pair<string, string> toStat(LatRet latRet, string p) {
    char fmt[1024] ;
    char prefix[100];
    strcpy(prefix, p.c_str());
    snprintf(fmt, sizeof(fmt), "%sp50,%sp75Nanos,%sp90Nanos,%sp95Nanos,%sp99Nanos,%sp99.9Nanos,%smaxNanos,%scount", prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix);
    string s1(fmt);

    snprintf(fmt, sizeof(fmt), "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%ld,%ld", latRet.nanos[1], latRet.nanos[2], latRet.nanos[3], latRet.nanos[4], latRet.nanos[5], latRet.nanos[6], latRet.maxNanos, latRet.total);

    string s2(fmt);

    return make_pair(s1, s2);
}


// print a vector
// coz !@$@!$ c++ doesnt provide one
void pv(vector<string>& v) {
    for (const auto &it : v)
        std::cout << ' ' << it;
    cout<<endl;
}


class ServerRmi : public xmlrpc_c::method {
private:
    unique_ptr<NioPerfTest> proxy{nullptr};
public:
    explicit ServerRmi(NioPerfTest *ref) {
        proxy.reset(ref);
    }

    void
    execute(xmlrpc_c::paramList const &paramList,
            xmlrpc_c::value *const retvalP) override {
        unsigned int arg=0;
        string methodName = paramList.getString(arg++);
        if (methodName == "serverInit") {
            proxy->serverInit(paramList.getString(arg++), paramList.getInt(arg++));
            paramList.verifyEnd(arg);
            *retvalP = xmlrpc_c::value_int(0);
        } else if(methodName == "clientInit") {
            proxy->clientInit(paramList.getString(arg++));
            paramList.verifyEnd(arg);
            *retvalP = xmlrpc_c::value_int(0);
        } else if(methodName == "serverWaitForClientConnect") {
            proxy->serverWaitForClientConnect();
            paramList.verifyEnd(arg);
            *retvalP = xmlrpc_c::value_int(0);
        } else if(methodName == "serverStartNioLoops") {
            proxy->serverStartNioLoops();
            paramList.verifyEnd(arg);
            *retvalP = xmlrpc_c::value_int(0);
        }  else if(methodName == "clientStart") {
            proxy->clientStart();
            paramList.verifyEnd(arg);
            *retvalP = xmlrpc_c::value_int(0);
        }  else if(methodName == "serverResult") {
            string ret = proxy->serverResult();
            paramList.verifyEnd(arg);
            *retvalP = xmlrpc_c::value_string(ret);
        }  else if(methodName == "clientResult") {
            string ret = proxy->clientResult();
            paramList.verifyEnd(arg);
            *retvalP = xmlrpc_c::value_string(ret);
        }
    }
};


void rpcServer(xmlrpc_c::method* api, const unsigned int port, string method) {
    try {
        xmlrpc_c::registry myRegistry;
        xmlrpc_c::methodPtr const sampleAddMethodP(api);
        myRegistry.addMethod(method, sampleAddMethodP);
        xmlrpc_c::serverAbyss myAbyssServer(
                xmlrpc_c::serverAbyss::constrOpt()
                        .registryP(&myRegistry)
                        .portNumber(port));
        cout<<"starting rpc "<<method<<" on port:"<<port<<endl;
        myAbyssServer.run();
        // xmlrpc_c::serverAbyss.run() never returns
        assert(false);
    } catch (exception const& e) {
        cerr << "Something failed.  " << e.what() << endl;
    }
}

void NioPerfTest::go(unsigned long argc, const char **argv) {
    vector<string> args(argv+1, argv+argc);
    if (argc==0) {
        throw ("Usage: java NetPerfNio type <client|gateway|server> clientRmis <ip:port>[,<ip:port>] serverRmi <ip:port> serverReal <ip:port> time <secs[default=10]> gatewayRmiPort <default:5000> serverRmiPort <default:5001> clientRmiPort<default:5002> ipAddr <ipaddress for rmi <optional)");
    }
    pv(args);
    int argIndex = 0;
    while(argIndex < args.size()) {
        const string& arg = args[argIndex];
        if(arg.find("type")!=string::npos) {
            type = args[++argIndex];
            argIndex++;
        } else if(arg == "serverReal") {
            serverReal =  parseIpPort(args[++argIndex], 8000)[0];
            argIndex++;
        }  else if(arg == "serverRmi") {
            serverRmi = parseIpPort(args[++argIndex], serverRmiPort)[0];

            argIndex++;
        } else if(arg == "clientRmis") {
            clientRmis = parseIpPort(args[++argIndex], clientRmiPort);
            argIndex++;
        } else if(arg == "time") {
            timeMs = stoi(args[++argIndex])*1000;

            argIndex++;
        } else if(arg == "gatewayRmiPort") {
            gatewayRmiPort = stoi(args[++argIndex]);
            argIndex++;
        } else if(arg == "serverRmiPort") {
            serverRmiPort = stoi(args[++argIndex]);
            argIndex++;
        } else if(arg == "clientRmiPort") {
            clientRmiPort = stoi(args[++argIndex]);
            argIndex++;
        } else if(arg == "ipAddr") {
            ipAddr = (args[++argIndex]);
            argIndex++;
        } else {
            throw ("unknown arg: "+args[argIndex] );
        }

    }

    // the rpc server is common for all three daemons: gateway, client and server
    // server rpc methods start with serverXXX, client rpc methods start with clientXXX and so on.


    if(type == "server") {
        server();
    } else if(type == "client") {
        client();
    } else if(type == "gateway") {
        gateway();
    }
}


// becuae !@#$%^&* c++ doesnt have one.
void split(const string& s, char delim,vector<string>& v) {
    auto i = 0L;
    while(true) {
        auto pos = s.find(delim, i);
        if(pos==i+1) continue;
        if (pos == string::npos) {
            v.push_back(s.substr(i, s.length()));
            return;
        }
        v.push_back(s.substr(i, pos-i));
        i = ++pos;
    }
}


void doTest() {
    {
        auto *server = new NioPerfTest;
        string s = "progname type server time 10 serverReal 127.0.0.1:8000";
        vector<string> v;
        split(s, ' ', v);
        const char *argv[v.size()];
        for(int i=0; i<v.size(); i++) {
            argv[i] = strdup(v[i].c_str());
        }
        // go will create a new thread to start listening
        server->go(v.size(), argv);
    }

    ev_sleep(1.0);

    int clients = 1;
    string a;
    for(int i=0; i<clients; i++) {
        a+="127.0.0.1:"+to_string(i+5002);
        if(i<clients-1) {
            a+=",";
        }
        auto *client = new NioPerfTest;
//    string s = "prog_name_placeholder type server time 10 serverReal 127.0.0.1:8000";
        string s = "progname type client serverReal 127.0.0.1:8000 clientRmiPort "+to_string(i+5002);
        vector<string> v;
        split(s, ' ', v);
        const char *argv[v.size()];
        for(int j=0; j<v.size(); j++) {
            argv[j] = strdup(v[j].c_str());
        }
        client->go(v.size(), argv);
    }

    sleep(1);

    auto *gateway = new NioPerfTest;
//    string s = "prog_name_placeholder type server time 10 serverReal 127.0.0.1:8000";
    string s = "progname type gateway serverRmi 127.0.0.1:5001 clientRmis "+a;
    vector<string> v;
    split(s, ' ', v);
    const char *argv[v.size()];
    for(int i=0; i<v.size(); i++) {
        argv[i] = strdup(v[i].c_str());
    }
    gateway->go(v.size(), argv);
}

vector<IpPort> NioPerfTest::parseIpPort(const string& str, int defaultPort) {
    vector<string> vec;
    split(str, ',', vec);
    vector<IpPort> ret;

    for (auto s1 : vec) {
        if(s1.find(':')!=string::npos) {
            vector<string>xx;
            split(s1, ':', xx);
            ret.emplace_back(xx[0], stoi(xx[1]));
        } else {
            ret.emplace_back(s1, defaultPort);
        }
    }
    return ret;
}

void NioPerfTest::server() {
    cout<<"came in server. This is "<<(long)this<<endl;

    thread t([this]() {
        ServerRmi rmi(this);
        rpcServer(&rmi, (unsigned int)serverRmiPort, string("server"));
    });
    t.detach();

}

void NioPerfTest::client() {
    cout<<"came in client. This is "<<(long)this<<endl;

    thread t([this]() {
        ServerRmi rmi(this);
        cout<<"client listening on "<<clientRmiPort<<endl;
        rpcServer(&rmi, (unsigned int)clientRmiPort, string("myclient"));
    });
    t.detach();

}

void NioPerfTest::gateway() {
    string s = "http://"+string(serverRmi.ip)+":"+to_string(serverRmi.port)+"/RPC2";
    cout<<"gateway server url is "<<s<<endl;
    string const &serverUrl = s;

    vector<string> clientUrls;
    for(const auto &rmi : clientRmis) {
        string x("http://"+rmi.ip+":"+to_string(rmi.port)+"/RPC2");
        cout<<"gateway client url is "<<x<<endl;

        clientUrls.push_back(x);
    }


    std::ifstream input("/tmp/niotests");
    std::stringstream sstr;
    while(input >> sstr.rdbuf());
    auto j3 = nlohmann::json::parse(sstr.str());
    for(auto &obj : j3) {
        if(obj["completed"]) {
            continue;
        }
        string testcase = obj.dump(4);
        {
            xmlrpc_c::clientSimple rmi;
            xmlrpc_c::value result;
            xmlrpc_c::paramList myParams;
            auto sz = (int)clientUrls.size();
            cout<<"gateway client sz "<<sz<<endl;
            rmi.call(serverUrl, "server", myParams.addc("serverInit").addc(testcase).addc(sz), &result);
        }

        {
            vector<thread> vec;
            for (auto const &uri : clientUrls) {
                vec.emplace_back([testcase, &uri]() {
                    xmlrpc_c::clientSimple rmi;
                    xmlrpc_c::value result;
                    xmlrpc_c::paramList myParams;
                    rmi.call(uri, "myclient", myParams.addc("clientInit").addc(testcase), &result);
                });
            }
            for (auto &t:vec) {
                t.join();
            }
        }

        {
            xmlrpc_c::clientSimple rmi;
            xmlrpc_c::value result;
            xmlrpc_c::paramList myParams;
            rmi.call(serverUrl, "server", myParams.addc("serverWaitForClientConnect"), &result);
        }


        {
            xmlrpc_c::clientSimple rmi;
            xmlrpc_c::value result;
            xmlrpc_c::paramList myParams;
            rmi.call(serverUrl, "server", myParams.addc("serverStartNioLoops"), &result);
        }
        sc{}<<"gateway checkpoint"<<endl;
        {
            vector<thread> vec;
            for (auto const &uri : clientUrls) {
                vec.emplace_back([testcase, &uri]() {
                    xmlrpc_c::clientSimple rmi;
                    xmlrpc_c::value result;
                    xmlrpc_c::paramList myParams;
                    rmi.call(uri, "myclient", myParams.addc("clientStart"), &result);
                });
            }
            for (auto &t:vec) {
                t.join();
            }
        }

        ev_sleep(timeMs/1000.0);
        {
//            vector<nlohmann::json> clientRtt;
            nlohmann::json clientRtt;
            for (auto const &uri : clientUrls) {
                xmlrpc_c::clientSimple rmi;
                xmlrpc_c::value result;
                xmlrpc_c::paramList myParams;
                rmi.call(uri, "myclient", myParams.addc("clientResult"), &result);
                string r = xmlrpc_c::value_string(result);
                clientRtt.push_back(r);
            }
            obj["clientRtt"] = clientRtt;
        }

        {
            xmlrpc_c::clientSimple rmi;
            xmlrpc_c::value result;
            xmlrpc_c::paramList myParams;
            rmi.call(serverUrl, "server", myParams.addc("serverResult"), &result);
            string r = xmlrpc_c::value_string(result);
            sc{}<<"gateway: server result returned"<<r<<endl;
            auto rjson = nlohmann::json::parse(r);
            obj["serverNioLoops"] = rjson;
        }

        sc{}<<"bleh"<<obj<<endl;
//        return;

        std::ofstream o("/tmp/niotests_new");
        o << std::setw(4) << j3 << std::endl;
        o.flush();
        o.close();
        // cool down for 5 seconds
        ev_sleep(5.0);
//        return;


//        cout<<__LINE__<<" gatway starting a test case"<<endl;
    }

}


void NioPerfTest::clientInit(const string & testcase) {
    sc{}<<"client side clientInit"<<testcase<<" bigfatlock is "<<(long)&bigFatLock<<"This is "<<(long)this<<endl;
    std::unique_lock<std::mutex> lk(bigFatLock);
    sc{}<<"client side clientInit acquired the bigfatlock"<<testcase<<endl;

    testcaseObj = nlohmann::json::parse(testcase);
    int connectionsPerClient = testcaseObj["connectionsPerClient"];

    clientStates.clear();
    clientThreads.clear();

    for(int i=0; i<connectionsPerClient; i++) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
            perror("ERROR opening client socket");
        struct sockaddr_in serv_addr;
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(serverReal.port);
        if(inet_pton(AF_INET, serverReal.ip.c_str(), &serv_addr.sin_addr)<=0) {
            perror("could not connect");
        }
        if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
            perror("client ERROR connecting");
        }
        //sleep for 10 ms
        ev_sleep(0.3);
        sc{}<<"client: connected to server."<<endl;
        clientStates.emplace_back();
        clientStates[clientStates.size()-1].sockfd = sockfd;
    }
}

void NioPerfTest::clientStart() {
    sc{}<<"client side clientStart"<<endl;
    std::unique_lock<std::mutex> lk(bigFatLock);
    for(auto &clientState : clientStates) {
        if(testcaseObj["tt"]=="TCP_STREAM") {
            clientThreads.emplace_back([this, &clientState](){
                this->client_tcp_stream(clientState);
            });

        } else {
            clientThreads.emplace_back([this, &clientState](){
                this->client_tcp_rr(clientState);
            });
        }
    }
}

void NioPerfTest::client_tcp_stream(ClientState& clientState) {
    sc{}<<"client side start tcp stream"<<endl;
    int msg = testcaseObj["msg"];
    int fd = clientState.sockfd;
    long long bytesWritten = 0;
    auto now = std::chrono::high_resolution_clock::now();
    int count = 0;
    char buffer[msg];
    while(true) {
        if ((count &64)==0) {
            auto tmp = std::chrono::high_resolution_clock::now();
            auto durationMillis = std::chrono::duration_cast<std::chrono::milliseconds>(tmp-now).count();
            if(durationMillis >= timeMs) {
                break;
            }
        }
        long bytesNow = write(fd, buffer, sizeof(buffer));

        if(bytesNow < 0) {
            perror("something went wrong in tcp_stream client write");
            break;
        }
        bytesWritten += bytesNow;

        count++;
    }
    sc{}<<"client side fini"<<endl;

    auto end = std::chrono::high_resolution_clock::now();

    auto durationMillis = std::chrono::duration_cast<std::chrono::milliseconds>(end-now).count();
    clientState.bytes = bytesWritten;
    clientState.durationMillis = durationMillis;
}

void NioPerfTest::client_tcp_rr(ClientState& clientState) {
    sc{}<<"client side start tcp stream"<<endl;
    int msg = testcaseObj["msg"];
    int fd = clientState.sockfd;
    long long bytesWritten = 0;
    auto now = std::chrono::high_resolution_clock::now();
    int count = 0;
    char buffer[msg];
    while(true) {
        if ((count &64)==0) {
            auto tmp = std::chrono::high_resolution_clock::now();
            auto durationMillis = std::chrono::duration_cast<std::chrono::milliseconds>(tmp-now).count();
            if(durationMillis >= timeMs) {
                break;
            }
        }
        long tot = 0;
        while(tot < msg) {
            long bytesNow = write(fd, buffer, sizeof(buffer));
            if(bytesNow <= 0) {
                perror("something went wrong in tcp_stream client write");
                break;
            }
            bytesWritten += bytesNow;
            tot+=bytesNow;
        }

        tot = 0;
        while(tot < msg) {
            long bytesNow = read(fd, buffer, sizeof(buffer));
            if(bytesNow <= 0) {
                perror("something went wrong in tcp_stream client write");
                break;
            }
            tot+=bytesNow;
        }
        count++;
    }
    sc{}<<"client side fini"<<endl;
    auto end = std::chrono::high_resolution_clock::now();
    auto durationMillis = std::chrono::duration_cast<std::chrono::milliseconds>(end-now).count();
    clientState.bytes = bytesWritten;
    clientState.durationMillis = durationMillis;
}


string NioPerfTest::clientResult() {
    std::unique_lock<std::mutex> lk(bigFatLock);

//    vector<ClientState> clientStates;
//    vector<thread> clientThreads;
    for(auto &t: clientThreads) {
        t.join();
    }
    char buf[1024];
    string sb = "bytes,durationMillis,p50,p75Nanos,p90Nanos,p95Nanos,p99Nanos,p99.9Nanos,maxNanos,count";
    for(auto &state: clientStates) {
        close(state.sockfd);
        //sleep for 20 ms between closes (dont know why)
        ev_sleep(0.02);
        auto p = toStat(state.rttLat.snap(), "");
        snprintf(buf, sizeof(buf), "\n%ld,%ld,%s", state.bytes, state.durationMillis, p.second.c_str());
        sb.append(buf);
    }
    sc{}<<"client side clientResult "<<sb.c_str()<<endl;
    return sb;
}


void NioPerfTest::serverInit(const string &testcase, int numClients) {
    std::unique_lock<std::mutex> lk(bigFatLock);
    numClientsConnected = 0;
    nioForLoops.clear();
    nioForLoopThreads.clear();

    testcaseObj = nlohmann::json::parse(testcase);
    int connectionsPerClient = testcaseObj["connectionsPerClient"];
    totClients = connectionsPerClient*numClients;
    int nioloops = testcaseObj["nioloops"];
    int msg = testcaseObj["msg"];
    if(testcaseObj.count("time") > 0) {
        timeMs = testcaseObj["time"];
    }

    //create the event loops
    for(int i=0; i<nioloops; i++) {
        nioForLoops.emplace_back();
        NioForLoop& nioForLoop = nioForLoops[nioForLoops.size()-1];
        struct ev_loop *loop = ev_loop_new();
        nioForLoop.loop = loop;

        nioForLoop.prepare.prepare.data = this;
        nioForLoop.prepare.pNioForLoop = &nioForLoop;
        ev_prepare_init((struct ev_prepare *)&nioForLoop.prepare, prepare_cb);
        ev_prepare_start(nioForLoop.loop, &nioForLoop.prepare.prepare);


        nioForLoop.check.check.data = this;
        nioForLoop.check.pNioForLoop = &nioForLoop;
        ev_check_init((struct ev_check *)&nioForLoop.check, check_cb);
        ev_check_start(nioForLoop.loop, &nioForLoop.check.check);
    }

    // prepare for listening
    int sockfd =  socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        perror("ERROR opening socket");
    int yes = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))==-1) {
        perror("server side setsockopt reuseaddr failed");
    };


    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(serverReal.port);
    if (conflict_bind(sockfd, (struct sockaddr *) &serv_addr,
                      sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
    }
    listen(sockfd,1024);


    // start the listening thread
    thread t([sockfd, this]() {
        sc{}<<"server listen callback fired"<<" bigfatlock is "<<(long)&bigFatLock<<"This is "<<(long)this<<endl;
        std::unique_lock<std::mutex> lk1(bigFatLock);
        sc{}<<"server listen callback finished acquiring the lock"<<endl;

        ev_io stdin_watcher;

        stdin_watcher.data = this;

        // use the default event loop unless you have special needs
        struct ev_loop *loop = EV_DEFAULT;

        // initialise an io watcher, then start it
        // this one will watch for stdin to become readable
        ev_io_init (&stdin_watcher, stdin_cb, sockfd, EV_READ);
        ev_io_start (loop, &stdin_watcher);

        // now wait for events to arrive
        sc{} <<"server: starting listen loop"<<endl;
        ev_run (loop, 0);
        sc{}<<"finished the listen loop"<<endl;
        ev_io_stop(EV_DEFAULT, &stdin_watcher);
        close(sockfd);
    });
    t.detach();
    sc{}<<"server returning from serverinit"<<endl;
}

void NioPerfTest::serverListenCb(EV_P_ ev_io *w, int revents) {
    //NOTE: This thread already has the bigFatLock

    // the accepting part
    struct sockaddr_storage cli_addr;
    socklen_t clilen = sizeof(struct sockaddr_storage);
    int newsockfd = accept(w->fd,
                           (struct sockaddr *) &cli_addr, &clilen);
    if (newsockfd < 0) {
        perror("ERROR on accept");
    }
    sc{}<<"server sockfd is "<<newsockfd<<endl;
    char hoststr[NI_MAXHOST];
    char portstr[NI_MAXSERV];
    getnameinfo((struct sockaddr *)&cli_addr,
                clilen, hoststr, sizeof(hoststr), portstr, sizeof(portstr),
                NI_NUMERICHOST | NI_NUMERICSERV);


    fcntl(newsockfd, F_SETFL, O_NONBLOCK);
    auto whichLoop  = numClientsConnected % nioForLoops.size();
    NioForLoop& nioForLoop = nioForLoops[whichLoop];
    nioForLoops[whichLoop].connStates.emplace_back();
    ServerConnState& state = nioForLoops[whichLoop].connStates[nioForLoops[whichLoop].connStates.size()-1];
    state.pNioForLoop = &nioForLoop;
    state.clientIp=string(hoststr);
    state.clientIp+=":";
    state.clientIp+=string(portstr);
    int x = testcaseObj["msg"];
    state.buffer.reserve(x);
    state.bytesReadSoFar = 0;
    state.bytes = 0;
    state.writing = false;
    state.writeInterest = false;
    for(int i=0; i<x; i++) {
        state.buffer.push_back('a');
    }


    state.watcher.data = this;
    if(testcaseObj["tt"]=="TCP_STREAM") {
        ev_io_init (&state.watcher, tcp_stream_cb, newsockfd, EV_READ);
    } else {
        ev_io_init (&state.watcher, tcp_rr_cb, newsockfd, EV_READ);
    }

    ev_io_start (nioForLoop.loop, &state.watcher);
    {
        std::unique_lock<std::mutex> lk(serverInitMutex);
        numClientsConnected++;
    }
    if(numClientsConnected == totClients) {
        serverInitCv.notify_all();
        // all have connected. we can close
        ev_break (EV_A_ EVBREAK_ONE);
    }
    sc{} << "server: accepted a client: "<<numClientsConnected<<" to loop "<<whichLoop<<endl;
}

void NioPerfTest::serverPrepareCb(struct ev_loop *loop, ev_prepare *w, int revents) {
    auto pprepare = (struct my_ev_prepare *)w;
    auto nioForLoop = pprepare->pNioForLoop;
    auto tmp = std::chrono::high_resolution_clock::now();
    auto durationNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp-nioForLoop->tp).count();
    nioForLoop->totalLat.count(durationNanos);
    nioForLoop->tp = tmp;
}

void NioPerfTest::serverCheckCb(struct ev_loop *loop, ev_check *w, int revents) {
    auto pcheck = (struct my_ev_check *)w;
    auto nioForLoop = pcheck->pNioForLoop;
    auto tmp = std::chrono::high_resolution_clock::now();
    auto durationNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp-nioForLoop->tp).count();
    nioForLoop->selectLat.count(durationNanos);
}


void NioPerfTest::serverWaitForClientConnect() {
    sc{}<<"serverWaitForClientConnect waitign for notification"<<endl;

    std::unique_lock<std::mutex> lk(serverInitMutex);
    serverInitCv.wait(lk, [a(numClientsConnected), b(totClients)]{return a==b;});
    sc{}<<"serverWaitForClientConnect acquired serverInitMutex"<<endl;
    std::unique_lock<std::mutex> lk1(bigFatLock);
    sc{}<<"server side finished for client connect"<<endl;
}

void NioPerfTest::serverStartNioLoops() {
    sc{}<<"server side serverStartNioLoops"<<endl;
    std::unique_lock<std::mutex> lk(bigFatLock);
    for(auto &loop : nioForLoops) {
        loop.tp = std::chrono::high_resolution_clock::now();
        nioForLoopThreads.emplace_back([this, &loop](){
            this->serverRunLoop(loop);
        });
    }
//    for(thread& t: nioForLoopThreads) {
//        t.join();
//    }
}


void NioPerfTest::serverRunLoop(NioForLoop& nioForLoop) {
    int msg = testcaseObj["msg"];
    auto x = std::chrono::high_resolution_clock::now();
    sc{} << "starting a single nio loop with message size "<<msg<<endl;
    for(auto f:nioForLoop.connStates) {
        cout<<""<<endl;
    }
    ev_sleep(2);
    ev_run(nioForLoop.loop, 0);
    sc{} << "finished a single event loop. returning"<<endl;
    auto y = std::chrono::high_resolution_clock::now();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(y-x).count();

    for(auto &state:nioForLoop.connStates) {
        ev_io_stop(nioForLoop.loop, &state.watcher);
        close(state.watcher.fd);
        state.durationMillis = (nanos/1000);
    }
    ev_prepare_stop(nioForLoop.loop, &nioForLoop.prepare.prepare);
    ev_check_stop(nioForLoop.loop, &nioForLoop.check.check);
    ev_loop_destroy(nioForLoop.loop);
}

void NioPerfTest::server_tcp_stream_cb(struct ev_loop *loop, ev_io *w, int revents) {
//    sc{}<<"came in event lo/op"<<endl;
    auto pstate = (ServerConnState *)w;
    auto pNioForLoop = pstate->pNioForLoop;
    unsigned int msg = testcaseObj["msg"];

    // the first element is the beginning of the buffer
    // the standard guarantees that the elements are contiguous
    char *buffer = &((pstate->buffer)[0]);

    while(pstate->bytesReadSoFar<msg) {
        if(msg - pstate->bytesReadSoFar > pstate->buffer.capacity()) {
            sc{}<<"fooooo"<<endl;
            ev_sleep(1);

        }
        long bytesNow = read(pstate->watcher.fd, buffer, msg - pstate->bytesReadSoFar);
        sc{}<<"read "<<bytesNow<<endl;
        // check for eof
        if(bytesNow==0) {
            ev_break(loop, EVBREAK_ONE);
            break;
        }


        if(bytesNow<0) {
            // nothing to read now
            if(errno!=EAGAIN) {
                // some unknown error, break the loop
                perror("Soemthing went wrong in read()");
                ev_break(loop, EVBREAK_ONE);
            }
            break;
        }
        pstate->bytesReadSoFar+=bytesNow;
        pNioForLoop->readLat.count();
        pstate->bytes+=bytesNow;
    }
    if(pstate->bytesReadSoFar==msg) {
        pstate->bytesReadSoFar=0;
    }
}

void NioPerfTest::server_tcp_rr_cb(struct ev_loop *loop, ev_io *w, int revents) {
    sc{}<<"server started tcp_rr"<<endl;

    auto pstate = (ServerConnState *)w;
    auto pNioForLoop = pstate->pNioForLoop;
    unsigned int msg = testcaseObj["msg"];

    // the first element is the beginning of the buffer
    // the standard guarantees that the elements are contiguous
    char *buffer = &pstate->buffer[0];

    auto start = std::chrono::high_resolution_clock::now();

    if(!pstate->writing) {
        while(true) {
            auto x = std::chrono::high_resolution_clock::now();

            long bytesNow = read(pstate->watcher.fd, buffer, msg-pstate->bytesReadSoFar);
            auto y = std::chrono::high_resolution_clock::now();
            auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(y-x).count();
            pNioForLoop->readLat.count(nanos);

            if(bytesNow >= 0) {
                pstate->bytesReadSoFar += bytesNow;
                pstate->bytes+=bytesNow;
            }

            if(pstate->bytesReadSoFar == msg) {
                //finished reading
                // came here implies bytesNow > 0
                break;
            }

            // check for eof
            if(bytesNow==0) {
                ev_break(loop, EVBREAK_ONE);
                return;
            }

            // came here implies bytesNow < 0
            // nothing to read now
            if(errno!=EAGAIN) {
                // some unknown error, break the loop
                perror("Soemthing went wrong in read() closing the nio loop");
                ev_break(loop, EVBREAK_ONE);
                return;
            }
        }
    }

    if(pstate->bytesReadSoFar==msg) {
        pstate->writing = !pstate->writing;
        pstate->bytesReadSoFar=0;
    }
    if(pstate->writing) {
        while(true) {
            auto x = std::chrono::high_resolution_clock::now();

            long bytesNow = write(pstate->watcher.fd, buffer, msg-pstate->bytesReadSoFar);
            auto y = std::chrono::high_resolution_clock::now();
            auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(y-x).count();
            pNioForLoop->writeLat.count(nanos);

            if(bytesNow>=0) {
                pstate->bytesReadSoFar += bytesNow;
            }

            if(pstate->bytesReadSoFar == msg) {
                // came here also imples that bytesNow > 0 so unarm the event if armed
                if(pstate->writeInterest) {
                    ev_io_stop(loop, w);
                    ev_io_set(w, w->fd, EV_READ);
                    pstate->writeInterest = false;
                }
                break;
            }
            if(bytesNow>0) {
                // more things to be written
                continue;
            }

            if(bytesNow == 0) {
                throw string("write bytesNow was zero. why? pstate->bytesReadsoFar:")+to_string(pstate->bytesReadSoFar)+" msg:"+to_string(msg)+" bytesNow:"+to_string(bytesNow);
            }

            // came here implies bytesNow < 0
            if(errno==EAGAIN ) {
                // not able to write now, arm the write event if not armed already
                if(!pstate->writeInterest) {
                    ev_io_stop(loop, w);
                    ev_io_set(w, w->fd, EV_WRITE|EV_READ);
                    pstate->writeInterest = true;
                }
            } else {
                // some unknown error, break the loop
                perror("Soemthing went wrong in server tcp_rr write()");
                ev_break(loop, EVBREAK_ONE);
            }
            break;

        }
    }

    if(pstate->bytesReadSoFar == msg) {
        pstate->bytesReadSoFar = 0;
        pstate->writing = !pstate->writing;
    }
}



string NioPerfTest::serverResult() {
    sc{}<<"server side serverResult"<<endl;
    std::unique_lock<std::mutex> lk(bigFatLock);
    sc{}<<"server before joining"<<endl;

    for(auto &nioForLoop:nioForLoopThreads) {
        nioForLoop.join();
    }
    sc{}<<"server after joining"<<endl;

    nlohmann::json js;

    for(auto &loop : nioForLoops) {
        string s1, s2;
        auto p = toStat(loop.selectLat.snap(), "selectLat_");
        s1 += p.first+","; s2+=p.second+",";

        p = toStat(loop.readLat.snap(), "readLat_");
        s1 += p.first+","; s2+=p.second+",";

        p = toStat(loop.readLat.snap(), "writeLat_");
        s1 += p.first+","; s2+=p.second+",";

        p = toStat(loop.totalLat.snap(), "totalLat_");
        s1 += p.first; s2+=p.second;
        js.push_back(s1.append("\n").append(s2));
    }
    sc{}<<"returning from server result"<<endl;
    return js.dump(2);
}

int main(int const /*unused*/, const char ** const /*unused*/) {
    doTest();
//    do1();
    return 0;
}
