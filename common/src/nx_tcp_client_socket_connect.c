/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/


/**************************************************************************/
/**************************************************************************/
/**                                                                       */
/** NetX Component                                                        */
/**                                                                       */
/**   Transmission Control Protocol (TCP)                                 */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/

#define NX_SOURCE_CODE


/* Include necessary system files.  */

#include "nx_api.h"
#include "nx_tcp.h"
#include "tx_thread.h"
#include "nx_ip.h"


/**************************************************************************/
/*                                                                        */
/*  FUNCTION                                               RELEASE        */
/*                                                                        */
/*    _nx_tcp_client_socket_connect                       PORTABLE C      */
/*                                                           6.1          */
/*  AUTHOR                                                                */
/*                                                                        */
/*    Yuxin Zhou, Microsoft Corporation                                   */
/*                                                                        */
/*  DESCRIPTION                                                           */
/*                                                                        */
/*    This function handles the connect request for the supplied socket.  */
/*    If bound and not connected, this function will send the first SYN   */
/*    message to the specified server to initiate the connection process. */
/*                                                                        */
/*  INPUT                                                                 */
/*                                                                        */
/*    socket_ptr                            Pointer to TCP client socket  */
/*    server_ip                             IP address of server          */
/*    server_port                           Port number of server         */
/*    wait_option                           Suspension option             */
/*                                                                        */
/*  OUTPUT                                                                */
/*                                                                        */
/*    status                                Completion status             */
/*                                                                        */
/*  CALLS                                                                 */
/*                                                                        */
/*    _nx_tcp_socket_thread_suspend         Suspend thread for connection */
/*    _nx_tcp_packet_send_syn               Send SYN packet               */
/*    _nx_ip_route_find                     Find a suitable outgoing      */
/*                                            interface.                  */
/*    tx_mutex_get                          Obtain protection             */
/*    tx_mutex_put                          Release protection            */
/*                                                                        */
/*  CALLED BY                                                             */
/*                                                                        */
/*    Application Code                                                    */
/*                                                                        */
/*  RELEASE HISTORY                                                       */
/*                                                                        */
/*    DATE              NAME                      DESCRIPTION             */
/*                                                                        */
/*  05-19-2020     Yuxin Zhou               Initial Version 6.0           */
/*  09-30-2020     Yuxin Zhou               Modified comment(s),          */
/*                                            resulting in version 6.1    */
/*                                                                        */
/**************************************************************************/
UINT  _nx_tcp_client_socket_connect(NX_TCP_SOCKET *socket_ptr, ULONG server_ip, UINT server_port, ULONG wait_option)
{

NX_IP        *ip_ptr;
NX_INTERFACE *outgoing_interface;


    outgoing_interface = NX_NULL;
    /* Setup IP pointer.  */
    ip_ptr =  socket_ptr -> nx_tcp_socket_ip_ptr;

    /* If trace is enabled, insert this event into the trace buffer.  */
    NX_TRACE_IN_LINE_INSERT(NX_TRACE_TCP_CLIENT_SOCKET_CONNECT, ip_ptr, socket_ptr, server_ip, server_port, NX_TRACE_TCP_EVENTS, 0, 0)

    /* Obtain the IP mutex so we initiate the connect.  */
    tx_mutex_get(&(ip_ptr -> nx_ip_protection), TX_WAIT_FOREVER);

    /* Determine if the socket has already been bound to port or if a socket bind is
       already pending from another thread.  */
    if (!socket_ptr -> nx_tcp_socket_bound_next)
    {

        /* Release protection.  */
        tx_mutex_put(&(ip_ptr -> nx_ip_protection));

        /* Return a not bound error code.  */
        return(NX_NOT_BOUND);
    }

    /* Determine if the socket is in a pre-connection state.  */
    if (socket_ptr -> nx_tcp_socket_state != NX_TCP_CLOSED)
    {

        /* Release protection.  */
        tx_mutex_put(&(ip_ptr -> nx_ip_protection));

        /* Return a not closed error code.  */
        return(NX_NOT_CLOSED);
    }

    /*
       Find out a suitable outgoing interface and the next hop address if the destination is not
       directly attached to the local interface.  Since TCP must operate on unicast address,
       there is no need to pass in a "hint" for outgoing interface.
     */

    if (_nx_ip_route_find(ip_ptr, server_ip, &outgoing_interface, &socket_ptr -> nx_tcp_socket_next_hop_address) != NX_SUCCESS)
    {
        /* Release protection.  */
        tx_mutex_put(&(ip_ptr -> nx_ip_protection));

        /* Return an IP address error code.  */
        return(NX_IP_ADDRESS_ERROR);
    }


#ifndef NX_DISABLE_TCP_INFO

    /* Increment the active connections count.  */
    ip_ptr -> nx_ip_tcp_active_connections++;

    /* Increment the TCP connections count.  */
    ip_ptr -> nx_ip_tcp_connections++;
#endif

    /* If trace is enabled, insert this event into the trace buffer.  */
    NX_TRACE_IN_LINE_INSERT(NX_TRACE_INTERNAL_TCP_STATE_CHANGE, ip_ptr, socket_ptr, socket_ptr -> nx_tcp_socket_state, NX_TCP_SYN_SENT, NX_TRACE_INTERNAL_EVENTS, 0, 0)

    /* Move the TCP state to Sequence Sent, the next state of an active open.  */
    socket_ptr -> nx_tcp_socket_state =  NX_TCP_SYN_SENT;

    /* Save the server port and server IP address.  */
    socket_ptr -> nx_tcp_socket_connect_ip =    server_ip;
    socket_ptr -> nx_tcp_socket_connect_port =  server_port;

    /* Initialize the maximum segment size based on the interface MTU. */
    if (outgoing_interface -> nx_interface_ip_mtu_size < (sizeof(NX_IP_HEADER) + sizeof(NX_TCP_HEADER)))
    {
        /* Interface MTU size is smaller than IP and TCP header size.  Invalid interface! */

#ifndef NX_DISABLE_TCP_INFO

        /* Reduce the active connections count.  */
        ip_ptr -> nx_ip_tcp_active_connections--;

        /* Reduce the TCP connections count.  */
        ip_ptr -> nx_ip_tcp_connections--;
#endif

        /* Restore the socket state. */
        socket_ptr -> nx_tcp_socket_state = NX_TCP_CLOSED;

        /* Reset server port and server IP address. */
        socket_ptr -> nx_tcp_socket_connect_ip = 0;
        socket_ptr -> nx_tcp_socket_connect_port = 0;

        /* Reset the next_hop_address information. */
        socket_ptr -> nx_tcp_socket_next_hop_address = 0;

        /* Release protection.  */
        tx_mutex_put(&(ip_ptr -> nx_ip_protection));


        /* Return an IP address error code.  */
        return(NX_INVALID_INTERFACE);
    }

    socket_ptr -> nx_tcp_socket_connect_interface = outgoing_interface;

    /* Setup the initial sequence number.  */
    if (socket_ptr -> nx_tcp_socket_tx_sequence == 0)
    {
        socket_ptr -> nx_tcp_socket_tx_sequence =  (((ULONG)NX_RAND()) << NX_SHIFT_BY_16) | ((ULONG)NX_RAND());
    }
    else
    {
        socket_ptr -> nx_tcp_socket_tx_sequence =  socket_ptr -> nx_tcp_socket_tx_sequence + ((ULONG)(((ULONG)0x10000))) + ((ULONG)NX_RAND());
    }

    /* Ensure the rx window size logic is reset.  */
    socket_ptr -> nx_tcp_socket_rx_window_current =    socket_ptr -> nx_tcp_socket_rx_window_default;
    socket_ptr -> nx_tcp_socket_rx_window_last_sent =  socket_ptr -> nx_tcp_socket_rx_window_default;

    /* Clear the FIN received flag.  */
    socket_ptr -> nx_tcp_socket_fin_received =  NX_FALSE;

    /* Increment the sequence number.  */
    socket_ptr -> nx_tcp_socket_tx_sequence++;

    /* Setup a timeout so the connection attempt can be sent again.  */
    socket_ptr -> nx_tcp_socket_timeout =          socket_ptr -> nx_tcp_socket_timeout_rate;
    socket_ptr -> nx_tcp_socket_timeout_retries =  0;

    /* CLEANUP: Clean up any existing socket data before making a new connection. */
    socket_ptr -> nx_tcp_socket_tx_window_congestion = 0;
    socket_ptr -> nx_tcp_socket_tx_outstanding_bytes = 0;
    socket_ptr -> nx_tcp_socket_packets_sent = 0;
    socket_ptr -> nx_tcp_socket_bytes_sent = 0;
    socket_ptr -> nx_tcp_socket_packets_received = 0;
    socket_ptr -> nx_tcp_socket_bytes_received = 0;
    socket_ptr -> nx_tcp_socket_retransmit_packets = 0;
    socket_ptr -> nx_tcp_socket_checksum_errors = 0;
    socket_ptr -> nx_tcp_socket_transmit_sent_head  =  NX_NULL;
    socket_ptr -> nx_tcp_socket_transmit_sent_tail  =  NX_NULL;
    socket_ptr -> nx_tcp_socket_transmit_sent_count =  0;
    socket_ptr -> nx_tcp_socket_receive_queue_count =  0;
    socket_ptr -> nx_tcp_socket_receive_queue_head  =  NX_NULL;
    socket_ptr -> nx_tcp_socket_receive_queue_tail  =  NX_NULL;

    /* Send the SYN message.  */
    _nx_tcp_packet_send_syn(socket_ptr, (socket_ptr -> nx_tcp_socket_tx_sequence - 1));

    /* Determine if the connection is complete.  This can only happen in a connection
       between ports on the same IP instance.  */
    if (socket_ptr -> nx_tcp_socket_state == NX_TCP_ESTABLISHED)
    {

        /* Release the protection.  */
        tx_mutex_put(&(ip_ptr -> nx_ip_protection));

        /* Return a successful status.  */
        return(NX_SUCCESS);
    }

    /* Optionally suspend the thread.  If timeout occurs, return a connection timeout status.  If
       immediate response is selected, return a connection in progress status.  Only on a real
       connection should success be returned.  */
    if ((wait_option) && (_tx_thread_current_ptr != &(ip_ptr -> nx_ip_thread)))
    {

        /* Suspend the thread on this socket's connection attempt.  */
        _nx_tcp_socket_thread_suspend(&(socket_ptr -> nx_tcp_socket_connect_suspended_thread), _nx_tcp_connect_cleanup, socket_ptr, &(ip_ptr -> nx_ip_protection), wait_option);

        /* Check if the socket connection has failed.  */
        if (_tx_thread_current_ptr -> tx_thread_suspend_status)
        {

            /* If trace is enabled, insert this event into the trace buffer.  */
            NX_TRACE_IN_LINE_INSERT(NX_TRACE_INTERNAL_TCP_STATE_CHANGE, ip_ptr, socket_ptr, socket_ptr -> nx_tcp_socket_state, NX_TCP_CLOSED, NX_TRACE_INTERNAL_EVENTS, 0, 0)

            /* Yes, socket connection has failed.  Return to the
               closed state so it can be tried again.  */
            socket_ptr -> nx_tcp_socket_state =  NX_TCP_CLOSED;
        }

        /* Just return the completion code.  */
        return(_tx_thread_current_ptr -> tx_thread_suspend_status);
    }
    else
    {

        /* No suspension is request, just release protection and return to the caller.  */

        /* Release the IP protection.  */
        tx_mutex_put(&(ip_ptr -> nx_ip_protection));

        /* Return in-progress completion status.  */
        return(NX_IN_PROGRESS);
    }
}

