#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/seq-ts-header.h"

using namespace ns3;

class GoBackNSender : public Application 
{
public:
  GoBackNSender ();
  virtual ~GoBackNSender ();
  
  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, Time timeout, uint32_t windowSize);
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
  uint32_t        m_windowSize;     

  uint32_t        m_base;           
  uint32_t        m_nextSeqNum;     
  EventId         m_timeoutEvent;   
  
  std::vector<Ptr<Packet>> m_windowBuffer; 
  uint32_t        m_totalBytesSent; 
};

GoBackNSender::GoBackNSender () : m_base(0), m_nextSeqNum(0), m_totalBytesSent(0) {}
GoBackNSender::~GoBackNSender () { m_socket = 0; }

void GoBackNSender::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, Time timeout, uint32_t windowSize)
{
  m_socket = socket; m_peerAddress = address; m_packetSize = packetSize;
  m_nPackets = nPackets; m_timeout = timeout; m_windowSize = windowSize;
}

void GoBackNSender::StartApplication (void)
{
  m_socket->Bind ();
  m_socket->Connect (m_peerAddress);
  m_socket->SetRecvCallback (MakeCallback (&GoBackNSender::ReceiveAck, this));
  SendPacket ();
}

void GoBackNSender::StopApplication (void)
{
  if (m_socket) { m_socket->Close (); }
  Simulator::Cancel (m_timeoutEvent);
}

void GoBackNSender::SendPacket ()
{
    while (m_nextSeqNum < m_base + m_windowSize && m_nextSeqNum < m_nPackets)
    {
        Ptr<Packet> packet = Create<Packet> (m_packetSize);
        SeqTsHeader seqTs;
        seqTs.SetSeq (m_nextSeqNum);
        packet->AddHeader (seqTs);

        m_windowBuffer.push_back (packet->Copy ()); 
        m_socket->Send (packet);
        m_totalBytesSent += packet->GetSize ();

        if (m_base == m_nextSeqNum)
        {
            m_timeoutEvent = Simulator::Schedule (m_timeout, &GoBackNSender::HandleTimeout, this);
        }
        m_nextSeqNum++; 
    }
}

void GoBackNSender::HandleTimeout ()
{
    for (uint32_t i = 0; i < m_windowBuffer.size (); i++)
    {
        m_socket->Send (m_windowBuffer[i]);
        m_totalBytesSent += m_windowBuffer[i]->GetSize (); 
    }
    m_timeoutEvent = Simulator::Schedule (m_timeout, &GoBackNSender::HandleTimeout, this);
}

void GoBackNSender::ReceiveAck (Ptr<Socket> socket)
{
    Address from;
    Ptr<Packet> packet = socket->RecvFrom (from); 

    SeqTsHeader seqTs;
    packet->RemoveHeader (seqTs);
    uint32_t ackNum = seqTs.GetSeq ();

    if (ackNum >= m_base)
    {
        uint32_t jumped = ackNum - m_base + 1; 
        m_windowBuffer.erase (m_windowBuffer.begin (), m_windowBuffer.begin () + jumped);
        m_base = ackNum + 1; 

        Simulator::Cancel (m_timeoutEvent); 

        if (m_base < m_nextSeqNum)
        {
            m_timeoutEvent = Simulator::Schedule (m_timeout, &GoBackNSender::HandleTimeout, this);
        }
        SendPacket ();
    }
}

class GoBackNReceiver : public Application 
{
public:
  GoBackNReceiver () : m_bytesReceived(0), m_totalDelay(0.0), m_packetsReceived(0), m_expectedSeqNum(0) {}
  void Setup (Ptr<Socket> socket) { m_socket = socket; }
  
  uint32_t GetBytesReceived () const { return m_bytesReceived; }
  double GetAverageDelay () const { 
      if (m_packetsReceived == 0) return 0;
      return m_totalDelay / m_packetsReceived; 
  }
  uint32_t GetPacketsReceived () const { return m_packetsReceived; }

private:
  virtual void StartApplication (void) override 
  {
    m_socket->SetRecvCallback (MakeCallback (&GoBackNReceiver::ReceivePacket, this));
  }
  virtual void StopApplication (void) override 
  {
    if (m_socket) m_socket->Close ();
  }

  void ReceivePacket (Ptr<Socket> socket) 
  {
    Address from;
    Ptr<Packet> packet = socket->RecvFrom (from); 
    
    SeqTsHeader seqTs;
    packet->RemoveHeader (seqTs); 
    uint32_t seqNum = seqTs.GetSeq ();

    if (seqNum == m_expectedSeqNum)
    {
        Time delay = Simulator::Now () - seqTs.GetTs (); 
        m_totalDelay += delay.GetSeconds ();
        m_bytesReceived += packet->GetSize ();
        m_packetsReceived++;
        m_expectedSeqNum++;
    }

    if (m_expectedSeqNum > 0)
    {
        Ptr<Packet> ack = Create<Packet> (2);
        SeqTsHeader ackHeader;
        ackHeader.SetSeq (m_expectedSeqNum - 1); 
        ack->AddHeader (ackHeader);
        socket->SendTo (ack, 0, from);
    }
  }

  Ptr<Socket> m_socket;
  uint32_t m_bytesReceived; 
  double m_totalDelay;       
  uint32_t m_packetsReceived; 
  uint32_t m_expectedSeqNum; 
};

struct SimResult { double goodput; double avgDelay; double utilization; uint32_t retransmissions; };

SimResult RunExperiment (double currentBer, std::string currentDelay, uint32_t windowSize)
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

    Ptr<GoBackNReceiver> receiverApp = CreateObject<GoBackNReceiver> ();
    receiverApp->Setup (recvSocket);
    nodes.Get (1)->AddApplication (receiverApp);
    receiverApp->SetStartTime (Seconds (1.0));
    receiverApp->SetStopTime (Seconds (10.0));

    Ptr<GoBackNSender> senderApp = CreateObject<GoBackNSender> ();
    Address targetAddress = InetSocketAddress (interfaces.GetAddress (1), 9);
    senderApp->Setup (sendSocket, targetAddress, 1024, 10000, Seconds (0.1), windowSize);
    nodes.Get (0)->AddApplication (senderApp);
    senderApp->SetStartTime (Seconds (2.0));
    senderApp->SetStopTime (Seconds (10.0));

    Simulator::Run ();

    double activeTime = 8.0; double linkCapacityBps = 5000000.0; 
    
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
    double defaultBer = 0.00001; std::string defaultDelay = "2ms"; uint32_t defaultWindow = 8;

    std::vector<double> berValues = {0.0, 5e-06, 1e-05, 2e-05, 3e-05, 5e-05, 7e-05, 1e-04, 2e-04, 3e-04, 5e-04, 7e-04, 1e-03};    std::ofstream fBer("gbn_ber.csv"); fBer << "BER,Goodput,AvgDelay,Utilization,Retransmissions\n";
    std::cout << "\nRunning GBN Varying BER..." << std::endl;
    for (double ber : berValues) {
        SimResult res = RunExperiment(ber, defaultDelay, defaultWindow);
        fBer << ber << "," << res.goodput << "," << res.avgDelay << "," << res.utilization << "," << res.retransmissions << "\n";
        std::cout << "BER: " << ber << " -> Done" << std::endl;
    }
    fBer.close();

    std::vector<std::string> delayValues = {"1ms", "2ms", "5ms", "10ms", "20ms", "30ms", "40ms", "50ms", "60ms", "70ms", "80ms", "90ms", "100ms"};
    std::ofstream fDelay("gbn_delay.csv"); fDelay << "Delay,Goodput,AvgDelay,Utilization,Retransmissions\n";
    std::cout << "Running GBN Varying Delay..." << std::endl;
    for (std::string delay : delayValues) {
        SimResult res = RunExperiment(defaultBer, delay, defaultWindow);
        fDelay << delay << "," << res.goodput << "," << res.avgDelay << "," << res.utilization << "," << res.retransmissions << "\n";
        std::cout << "Delay: " << delay << " -> Done" << std::endl;
    }
    fDelay.close();

    std::vector<uint32_t> windowValues = {1, 2, 4, 8, 16, 32, 48, 64, 96, 128, 256};
    std::ofstream fWin("gbn_window.csv"); fWin << "Window,Goodput,AvgDelay,Utilization,Retransmissions\n";
    std::cout << "Running GBN Varying Window..." << std::endl;
    for (uint32_t win : windowValues) {
        SimResult res = RunExperiment(defaultBer, defaultDelay, win);
        fWin << win << "," << res.goodput << "," << res.avgDelay << "," << res.utilization << "," << res.retransmissions << "\n";
        std::cout << "Window: " << win << " -> Done" << std::endl;
    }
    fWin.close();

    std::cout << "\nGo-Back-N Data Exported to CSV successfully!" << std::endl;
    return 0;
}