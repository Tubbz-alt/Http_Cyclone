#include "core/socket.h"
#include "klee/klee.h"
#include "core/tcp_misc.h"
#include <assert.h>
#include <stdlib.h>
#include "model.h"
#include "core/tcp_timer.h"


void tcpStateTimeWait(Socket *socket, TcpHeader *segment, size_t length)
{
   //Debug message
   // TRACE_DEBUG("TCP FSM: TIME-WAIT state\r\n");

   //First check sequence number
   if(tcpCheckSequenceNumber(socket, segment, length))
      return;

   //Check the RST bit
   if(segment->flags & TCP_FLAG_RST)
   {
      //Enter CLOSED state
      tcpChangeState(socket, TCP_STATE_CLOSED);

      //Dispose the socket if the user does not have the ownership anymore
      if(!socket->ownedFlag)
      {
         //Delete the TCB
         tcpDeleteControlBlock(socket);
         //Mark the socket as closed
         socket->type = SOCKET_TYPE_UNUSED;
      }

      //Return immediately
      return;
   }

   //Check the SYN bit
   if(tcpCheckSyn(socket, segment, length))
      return;
   //If the ACK bit is off drop the segment and return
   if(!(segment->flags & TCP_FLAG_ACK))
      return;

   //The only thing that can arrive in this state is a retransmission
   //of the remote FIN. Acknowledge it and restart the 2 MSL timeout
   if(segment->flags & TCP_FLAG_FIN)
   {
      //Send an acknowledgment for the FIN
      tcpSendSegment(socket, TCP_FLAG_ACK, socket->sndNxt, socket->rcvNxt, 0, FALSE);
      //Restart the 2MSL timer
      tcpTimerStart(&socket->timeWaitTimer, TCP_2MSL_TIMER);
   }
}

int main()
{
    // Initialisation
    socketInit();

    Socket *socket;
    IpPseudoHeader *pseudoHeader;
    TcpHeader *segment;
    size_t length;
    uint32_t ackNum, seqNum;
    uint8_t flags, dataOffset;
    uint16_t checksum, srcPort;
    struct socketModel *sModel;

    segment = malloc(sizeof(TcpHeader) + sizeof(u_int8_t));
    pseudoHeader = malloc(sizeof(IpPseudoHeader));

    // creation of a TCP socket
    socket = socketOpen(SOCKET_TYPE_STREAM, SOCKET_IP_PROTO_TCP);

    // TODO: See if it is the correct way to process.
    // Maybe open the connection by following the real call of the function
    // is a better way to do.
    tcpChangeState(socket, TCP_STATE_TIME_WAIT);

    // Creation of a "random" incomming package
    // We assume the pseudo header is filled with ipv4 addresses

    klee_make_symbolic(&ackNum, sizeof(ackNum), "ackNum");
    klee_make_symbolic(&seqNum, sizeof(seqNum), "seqNum");
    klee_make_symbolic(&flags, sizeof(flags), "flags");
    klee_assume(0 <= flags && flags <= 31);
    klee_make_symbolic(&checksum, sizeof(checksum), "checksum");
    klee_make_symbolic(&dataOffset, sizeof(dataOffset), "dataOffset");
    klee_assume(0 <= dataOffset && dataOffset <= 15);
    klee_make_symbolic(&srcPort, sizeof(srcPort), "srcPort");
    // The port cannot be null
    klee_assume(srcPort != 0);

    segment->srcPort = srcPort;
    segment->destPort = socket->localPort;
    segment->seqNum = seqNum;
    segment->ackNum = ackNum;
    segment->reserved1 = 0;
    segment->dataOffset = dataOffset;
    segment->flags = flags;
    segment->reserved2 = 0;
    segment->window = 26883; // Voir ce que c'est exactement
    segment->checksum = checksum;
    segment->urgentPointer = 0;
    segment->options[0] = 0;

    klee_make_symbolic(&length, sizeof(length), "length");

    // We make a hard copy of the struct to check if the fields have
    // changed after the call to the function
    sModel = toSockModel(socket);

    klee_assert(socket->state == TCP_STATE_TIME_WAIT);

    tcpStateTimeWait(socket, segment, length);

    // printf("%d\n", socket->state);

    klee_assert(socket->type == SOCKET_TYPE_UNUSED ||
                (equalSocketModel(socket, sModel) &&
                (socket->state == TCP_STATE_TIME_WAIT ||
                 socket->state == TCP_STATE_CLOSED)));
}
