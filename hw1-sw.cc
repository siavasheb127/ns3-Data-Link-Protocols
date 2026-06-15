#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/seq-ts-header.h"
#include <fstream>
#include <vector>
#include <string>

using namespace ns3;

struct SimResult { 
    double goodput; 
    double avgDelay; 
    double utilization; 
    uint32_t retransmissions; 
};

class StopAndWaitSender : public Application 
{
public:
  StopAndWaitSender ();
  virtual ~StopAndWaitSender ();
  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, Time timeout);
  uint32_t GetTotalBytesSent () const { return m_totalBytesSent; }

private:
  virtual void StartApplication (void) override;
  virtual void StopApplication (void) override;

  void SendPacket ();                   
  void ReceiveAck (Ptr<Socket> socket); 
  void HandleTimeout ();                

  Ptr<Socket>     m_socket;         
  Address         m_peerAddress;    
  uint32_t        m_packetSize;     
  uint32_t        m_nPackets;       
  Time            m_timeout;        

  uint32_t        m_packetsSent;    
  uint8_t         m_seqNo;          
  EventId         m_timeoutEvent;   
  Ptr<Packet>     m_unackedPacket;  
  
  uint32_t        m_totalBytesSent; 
};

StopAndWaitSender::StopAndWaitSender () : m_packetsSent(0), m_seqNo(0), m_totalBytesSent(0) {}
StopAndWaitSender::~StopAndWaitSender () { m_socket = 0; }

void StopAndWaitSender::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, Time timeout)
{
  m_socket = socket; m_peerAddress = address; m_packetSize = packetSize;
  m_nPackets = nPackets; m_timeout = timeout;
}

void StopAndWaitSender::StartApplication (void)
{
  m_socket->Bind ();
  m_socket->Connect (m_peerAddress);
  m_socket->SetRecvCallback (MakeCallback (&StopAndWaitSender::ReceiveAck, this));
  SendPacket ();
}

void StopAndWaitSender::StopApplication (void)
{
  if (m_socket) { m_socket->Close (); }
  Simulator::Cancel (m_timeoutEvent);
}

void StopAndWaitSender::SendPacket ()
{
    Ptr<Packet> packet = Create<Packet> (m_packetSize);
    
    SeqTsHeader seqTs;
    seqTs.SetSeq (m_seqNo);
    packet->AddHeader (seqTs);

    m_unackedPacket = packet->Copy ();
    m_socket->Send (packet);
    
    m_totalBytesSent += packet->GetSize (); 
    m_timeoutEvent = Simulator::Schedule (m_timeout, &StopAndWaitSender::HandleTimeout, this);
}

void StopAndWaitSender::HandleTimeout ()
{
    m_socket->Send (m_unackedPacket);
    m_totalBytesSent += m_unackedPacket->GetSize (); 
    m_timeoutEvent = Simulator::Schedule (m_timeout, &StopAndWaitSender::HandleTimeout, this);
}

void StopAndWaitSender::ReceiveAck (Ptr<Socket> socket)
{
    Address from;
    Ptr<Packet> packet = socket->RecvFrom (from); 

    Simulator::Cancel (m_timeoutEvent);
    m_unackedPacket = 0;
    m_seqNo = 1 - m_seqNo; 
    m_packetsSent++;

    if (m_packetsSent < m_nPackets)
    {
        Simulator::Schedule (Seconds(0.001), &StopAndWaitSender::SendPacket, this);
    }
}

class StopAndWaitReceiver : public Application 
{
public:
  StopAndWaitReceiver () : m_bytesReceived(0), m_totalDelay(0.0), m_packetsReceived(0) {}
  void Setup (Ptr<Socket> socket) { m_socket = socket; }
  
  uint32_t GetBytesReceived () const { return m_bytesReceived; }
  double GetAverageDelay () const { 
      if (m_packetsReceived == 0) return 0;
      return m_totalDelay / m_packetsReceived; 
  }
  uint32_t GetPacketsReceived () const { return m_packetsReceived; }

private:
  virtual void StartApplication (void) override 
  { m_socket->SetRecvCallback (MakeCallback (&StopAndWaitReceiver::ReceivePacket, this)); }
  
  virtual void StopApplication (void) override 
  { if (m_socket) m_socket->Close (); }

  void ReceivePacket (Ptr<Socket> socket) 
  {
    Address from;
    Ptr<Packet> packet = socket->RecvFrom (from); 
    
    SeqTsHeader seqTs;
    packet->RemoveHeader (seqTs); 
    Time delay = Simulator::Now () - seqTs.GetTs (); 
    m_totalDelay += delay.GetSeconds ();
    m_packetsReceived++;

    m_bytesReceived += packet->GetSize ();

    Ptr<Packet> ack = Create<Packet> (2);
    socket->SendTo (ack, 0, from);
  }

  Ptr<Socket> m_socket;
  uint32_t m_bytesReceived; 
  double m_totalDelay;       
  uint32_t m_packetsReceived; 
};


SimResult RunExperiment (double currentBer, std::string currentDelay)
{
    LogComponentDisableAll (LOG_LEVEL_INFO);
    NodeContainer nodes; nodes.Create (2);
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
    p2p.SetChannelAttribute ("Delay", StringValue (currentDelay));
    NetDeviceContainer devices = p2p.Install (nodes);

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
    em->SetAttribute ("ErrorRate", DoubleValue (currentBer));
    em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_BIT"));
    devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

    InternetStackHelper stack; stack.Install (nodes);
    Ipv4AddressHelper address; address.SetBase ("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign (devices);

    TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
    Ptr<Socket> recvSocket = Socket::CreateSocket (nodes.Get (1), tid);
    InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), 9);
    recvSocket->Bind (local); 
    Ptr<Socket> sendSocket = Socket::CreateSocket (nodes.Get (0), tid);
    sendSocket->Bind (); 

    Ptr<StopAndWaitReceiver> receiverApp = CreateObject<StopAndWaitReceiver> ();
    receiverApp->Setup (recvSocket);
    nodes.Get (1)->AddApplication (receiverApp);
    receiverApp->SetStartTime (Seconds (1.0));
    receiverApp->SetStopTime (Seconds (10.0));

    Ptr<StopAndWaitSender> senderApp = CreateObject<StopAndWaitSender> ();
    Address targetAddress = InetSocketAddress (interfaces.GetAddress (1), 9);
    senderApp->Setup (sendSocket, targetAddress, 1024, 500, Seconds (0.1));
    nodes.Get (0)->AddApplication (senderApp);
    senderApp->SetStartTime (Seconds (2.0));
    senderApp->SetStopTime (Seconds (10.0));

    Simulator::Run ();

    double activeTime = 8.0; 
    double linkCapacityBps = 5000000.0; 
    
    SimResult res;
    uint32_t validBytes = receiverApp->GetBytesReceived ();
    res.goodput = (validBytes * 8.0) / activeTime;
    res.avgDelay = receiverApp->GetAverageDelay ();
    
    uint32_t totalSentBytes = senderApp->GetTotalBytesSent ();
    res.utilization = ((totalSentBytes * 8.0) / (linkCapacityBps * activeTime)) * 100.0;

    uint32_t totalPacketsSent = totalSentBytes / (1024 + 12); 
    res.retransmissions = totalPacketsSent > receiverApp->GetPacketsReceived () ? 
                          totalPacketsSent - receiverApp->GetPacketsReceived () : 0;

    Simulator::Destroy ();
    return res;
}

int main(int argc, char *argv[])
{
    double defaultBer = 0.00001; 
    std::string defaultDelay = "2ms"; 

  
    std::vector<double> berValues = {0.0, 5e-06, 1e-05, 2e-05, 3e-05, 5e-05, 7e-05, 1e-04, 2e-04, 3e-04, 5e-04, 7e-04, 1e-03};
    std::ofstream fBer("sw_ber.csv"); 
    fBer << "BER,Goodput,AvgDelay,Utilization,Retransmissions\n";
    std::cout << "\nRunning Stop-and-Wait Varying BER..." << std::endl;
    for (double ber : berValues) {
        SimResult res = RunExperiment(ber, defaultDelay);
        fBer << ber << "," << res.goodput << "," << res.avgDelay << "," << res.utilization << "," << res.retransmissions << "\n";
        std::cout << "BER: " << ber << " -> Done" << std::endl;
    }
    fBer.close();

    std::vector<std::string> delayValues = {"1ms", "2ms", "5ms", "10ms", "20ms", "30ms", "40ms", "50ms", "60ms", "70ms", "80ms", "90ms", "100ms"};
    std::ofstream fDelay("sw_delay.csv"); 
    fDelay << "Delay,Goodput,AvgDelay,Utilization,Retransmissions\n";
    std::cout << "Running Stop-and-Wait Varying Delay..." << std::endl;
    for (std::string delay : delayValues) {
        SimResult res = RunExperiment(defaultBer, delay);
        
      
        std::string numericDelay = delay.substr(0, delay.find("ms"));
        
        fDelay << numericDelay << "," << res.goodput << "," << res.avgDelay << "," << res.utilization << "," << res.retransmissions << "\n";
        std::cout << "Delay: " << delay << " -> Done" << std::endl;
    }
    fDelay.close();

    std::cout << "\nStop-and-Wait Data Exported to CSV successfully!" << std::endl;
    return 0;
}