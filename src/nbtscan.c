/*
# Copyright 1999-2003 Alla Bezroutchko <alla@inetcat.org>
# Copyright 2004      Jochen Friedrich <jochen@scram.de>
# Copyright 2008      Walter "Wallie" Jakob Doekes <walter@wjd.nu>
# Copyright 2015      Johan Eidenvall <bugreport@eidenvall.se>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#if HAVE_STDINT_H
#include <stdint.h>
#endif
#include "statusq.h"
#include "range.h"
#include "list.h"
#include "errors.h"
#include "time.h"

int quiet = 0;

static void
print_banner ( void )
{
  puts ( "\nNBTscan version 1.7.1.\n"
         "This is a free software and it comes with absolutely no "
         "warranty.\n"
         "You can use, distribute and modify it under terms of GNU GPL "
         "2+.\n\n" );
}

static void
usage ( void )
{
  puts ( "Usage:\nnbtscan [-v] [-d] [-e] [-l] [-t timeout] [-b bandwidth] "
         "[-r] [-q] [-s separator] [-m retransmits] (-f "
         "filename)|(<scan_range>) \n"
         "\t-v\t\tverbose output. Print all names received\n"
         "\t\t\tfrom each host\n"
         "\t-d\t\tdump packets. Print whole packet contents.\n"
         "\t-e\t\tFormat output in /etc/hosts format.\n"
         "\t-l\t\tFormat output in lmhosts format.\n"
         "\t\t\tCannot be used with -v, -s or -h options.\n"
         "\t-t timeout\twait timeout milliseconds for response.\n"
         "\t\t\tDefault 1000.\n"
         "\t-b bandwidth\tOutput throttling. Slow down output\n"
         "\t\t\tso that it uses no more that bandwidth bps.\n"
         "\t\t\tUseful on slow links, so that ougoing queries\n"
         "\t\t\tdon't get dropped.\n"
         "\t-r\t\tuse local port 137 for scans. Win95 boxes\n"
         "\t\t\trespond to this only.\n"
         "\t\t\tYou need to be root to use this option on Unix.\n"
         "\t-q\t\tSuppress banners and error messages,\n"
         "\t-s separator\tScript-friendly output. Don't print\n"
         "\t\t\tcolumn and record headers, separate fields with "
         "separator.\n"
         "\t-h\t\tPrint human-readable names for services.\n"
         "\t\t\tCan only be used with -v option.\n"
         "\t-m retransmits\tNumber of retransmits. Default 0.\n"
         "\t-f filename\tTake IP addresses to scan from file filename.\n"
         "\t\t\t-f - makes nbtscan take IP addresses from stdin.\n"
         "\t<scan_range>\twhat to scan. Can either be single IP\n"
         "\t\t\tlike 192.168.1.1 or\n"
         "\t\t\trange of addresses in one of two forms: \n"
         "\t\t\txxx.xxx.xxx.xxx/xx or xxx.xxx.xxx.xxx-xxx.\n"
         "Examples:\n"
         "\tnbtscan -r 192.168.1.0/24\n"
         "\t\tScans the whole C-class network.\n"
         "\tnbtscan 192.168.1.25-137\n"
         "\t\tScans a range from 192.168.1.25 to 192.168.1.137\n"
         "\tnbtscan -v -s : 192.168.1.0/24\n"
         "\t\tScans C-class network. Prints results in script-friendly\n"
         "\t\tformat using colon as field separator.\n"
         "\t\tProduces output like that:\n"
         "\t\t192.168.0.1:NT_SERVER:00U\n"
         "\t\t192.168.0.1:MY_DOMAIN:00G\n"
         "\t\t192.168.0.1:ADMINISTRATOR:03U\n"
         "\t\t192.168.0.2:OTHER_BOX:00U\n"
         "\t\t...\n"
         "\tnbtscan -f iplist\n"
         "\t\tScans IP addresses specified in file iplist." );
  exit ( 2 );
}

static int
set_range ( char *range_str, struct ip_range *range_struct )
{
  if ( is_ip ( range_str, range_struct ) )
    return 1;
  if ( is_range1 ( range_str, range_struct ) )
    return 1;
  if ( is_range2 ( range_str, range_struct ) )
    return 1;
  return 0;
}

static void
print_header ( void )
{
  printf ( "%-17s%-17s%-10s%-17s%-17s\n",
           "IP address",
           "NetBIOS Name",
           "Server",
           "User",
           "MAC address" );
  puts ( "-------------------------------------------------------------------"
         "-----------" );
}

#define DUP( code ) code, code

static void
print_nb_host_info_header ( const nbname_response_header_t *header )
{
  printf ( "Transaction ID: 0x%04x (%d)\n", DUP ( header->transaction_id ) );
  printf ( "Flags: 0x%04x (%d)\n", DUP ( header->flags ) );
  printf ( "Question count: 0x%04x (%d)\n", DUP ( header->question_count ) );
  printf ( "Answer count: 0x%04x (%d)\n", DUP ( header->answer_count ) );
  printf ( "Name service count: 0x%04x (%d)\n",
           DUP ( header->name_service_count ) );
  printf ( "Additional record count: 0x%04x (%d)\n",
           DUP ( header->additional_record_count ) );
  printf ( "Question name: %s\n", header->question_name );
  printf ( "Question type: 0x%04x (%d)\n", DUP ( header->question_type ) );
  printf ( "Question class: 0x%04x (%d)\n", DUP ( header->question_class ) );
  printf ( "Time to live: 0x%08x (%d)\n", DUP ( header->ttl ) );
  printf ( "Rdata length: 0x%04x (%d)\n", DUP ( header->rdata_length ) );
  printf ( "Number of names: 0x%02x (%d)\n", DUP ( header->number_of_names ) );
}

static void
print_nb_host_info_footer ( const nbname_response_footer_t *footer )
{
  printf ( "Adapter address: %02x:%02x:%02x:%02x:%02x:%02x\n",
           footer->adapter_address[0],
           footer->adapter_address[1],
           footer->adapter_address[2],
           footer->adapter_address[3],
           footer->adapter_address[4],
           footer->adapter_address[5] );

  printf ( "Version major: 0x%02x (%d)\n", DUP ( footer->version_major ) );
  printf ( "Version minor: 0x%02x (%d)\n", DUP ( footer->version_minor ) );
  printf ( "Duration: 0x%04x (%d)\n", DUP ( footer->duration ) );
  printf ( "FRMRs Received: 0x%04x (%d)\n", DUP ( footer->frmps_received ) );

  printf ( "FRMRs Transmitted: 0x%04x (%d)\n",
           DUP ( footer->frmps_transmitted ) );

  printf ( "IFrame Receive errors: 0x%04x (%d)\n",
           DUP ( footer->iframe_receive_errors ) );

  printf ( "Transmit aborts: 0x%04x (%d)\n", DUP ( footer->transmit_aborts ) );
  printf ( "Transmitted: 0x%08x (%d)\n", DUP ( footer->transmitted ) );
  printf ( "Received: 0x%08x (%d)\n", DUP ( footer->received ) );

  printf ( "IFrame transmit errors: 0x%04x (%d)\n",
           DUP ( footer->iframe_transmit_errors ) );

  printf ( "No receive buffers: 0x%04x (%d)\n",
           DUP ( footer->no_receive_buffer ) );

  printf ( "tl timeouts: 0x%04x (%d)\n", DUP ( footer->tl_timeouts ) );
  printf ( "ti timeouts: 0x%04x (%d)\n", DUP ( footer->ti_timeouts ) );
  printf ( "Free NCBS: 0x%04x (%d)\n", DUP ( footer->free_ncbs ) );
  printf ( "NCBS: 0x%04x (%d)\n", DUP ( footer->ncbs ) );
  printf ( "Max NCBS: 0x%04x (%d)\n", DUP ( footer->max_ncbs ) );

  printf ( "No transmit buffers: 0x%04x (%d)\n",
           DUP ( footer->no_transmit_buffers ) );

  printf ( "Max datagram: 0x%04x (%d)\n", DUP ( footer->max_datagram ) );

  printf ( "Pending sessions: 0x%04x (%d)\n",
           DUP ( footer->pending_sessions ) );

  printf ( "Max sessions: 0x%04x (%d)\n", DUP ( footer->max_sessions ) );
  printf ( "Packet sessions: 0x%04x (%d)\n", DUP ( footer->packet_sessions ) );
}

static void
d_print_hostinfo ( struct in_addr addr, const struct nb_host_info *hostinfo )
{
  int i;
  unsigned char service; /* 16th byte of NetBIOS name */
  char name[16];

  printf ( "\nPacket dump for Host %s:\n\n", inet_ntoa ( addr ) );
  if ( hostinfo->is_broken )
    printf ( "Incomplete packet, %d bytes long.\n", hostinfo->is_broken );

  if ( hostinfo->header )
    print_nb_host_info_header ( hostinfo->header );

  if ( hostinfo->names )
    {
      printf ( "Names received:\n" );
      for ( i = 0; i < hostinfo->header->number_of_names; i++ )
        {
          service = hostinfo->names[i].ascii_name[15];
          strncpy ( name, hostinfo->names[i].ascii_name, 15 );
          name[15] = 0;
          printf ( "%-17s Service: 0x%02x Flags: 0x%04x\n",
                   name,
                   service,
                   hostinfo->names[i].rr_flags );
        }
    }

  if ( hostinfo->footer )
    print_nb_host_info_footer ( hostinfo->footer );
}

static int
v_print_hostinfo ( struct in_addr addr,
                   const struct nb_host_info *hostinfo,
                   char *sf,
                   int hr )
{
  int i, unique;
  my_uint8_t service; /* 16th byte of NetBIOS name */
  char name[16];

  if ( !sf )
    {
      printf ( "\nNetBIOS Name Table for Host %s:\n\n", inet_ntoa ( addr ) );
      if ( hostinfo->is_broken )
        printf ( "Incomplete packet, %d bytes long.\n", hostinfo->is_broken );

      printf ( "%-17s%-17s%-17s\n", "Name", "Service", "Type" );
      printf ( "----------------------------------------\n" );
    }
  if ( hostinfo->header && hostinfo->names )
    {
      for ( i = 0; i < hostinfo->header->number_of_names; i++ )
        {
          service = hostinfo->names[i].ascii_name[15];
          strncpy ( name, hostinfo->names[i].ascii_name, 15 );
          name[15] = 0;
          unique = !( hostinfo->names[i].rr_flags & 0x0080 );
          if ( sf )
            {
              printf ( "%s%s%s%s", inet_ntoa ( addr ), sf, name, sf );
              if ( hr )
                printf ( "%s\n", getnbservicename ( service, unique, name ) );
              else
                {
                  printf ( "%02x", service );
                  if ( unique )
                    printf ( "U\n" );
                  else
                    printf ( "G\n" );
                }
            }
          else
            {
              printf ( "%-17s", name );
              if ( hr )
                printf ( "%s\n", getnbservicename ( service, unique, name ) );
              else
                {
                  printf ( "<%02x>", service );
                  if ( unique )
                    printf ( "             UNIQUE\n" );
                  else
                    printf ( "              GROUP\n" );
                }
            }
        }
    }

  if ( hostinfo->footer )
    {
      if ( sf )
        printf ( "%s%sMAC%s", inet_ntoa ( addr ), sf, sf );
      else
        printf ( "\nAdapter address: " );
      printf ( "%02x:%02x:%02x:%02x:%02x:%02x\n",
               hostinfo->footer->adapter_address[0],
               hostinfo->footer->adapter_address[1],
               hostinfo->footer->adapter_address[2],
               hostinfo->footer->adapter_address[3],
               hostinfo->footer->adapter_address[4],
               hostinfo->footer->adapter_address[5] );
    }
  if ( !sf )
    printf ( "----------------------------------------\n" );
  return 1;
}

static int
print_hostinfo ( struct in_addr addr, struct nb_host_info *hostinfo, char *sf )
{
  int i;
  unsigned char service; /* 16th byte of NetBIOS name */
  char comp_name[16], user_name[16];
  int is_server = 0;
  int unique;
  int first_name = 1;

  strncpy ( comp_name, "<unknown>", 15 );
  strncpy ( user_name, "<unknown>", 15 );
  if ( hostinfo->header && hostinfo->names )
    {
      for ( i = 0; i < hostinfo->header->number_of_names; i++ )
        {
          service = hostinfo->names[i].ascii_name[15];
          unique = !( hostinfo->names[i].rr_flags & 0x0080 );
          if ( service == 0 && unique && first_name )
            {
              /* Unique name, workstation service - this is computer name */
              strncpy ( comp_name, hostinfo->names[i].ascii_name, 15 );
              comp_name[15] = 0;
              first_name = 0;
            }
          if ( service == 0x20 && unique )
            {
              is_server = 1;
            }
          if ( service == 0x03 && unique )
            {
              strncpy ( user_name, hostinfo->names[i].ascii_name, 15 );
              user_name[15] = 0;
            }
        }
    }

  if ( sf )
    {
      printf ( "%s%s%s%s", inet_ntoa ( addr ), sf, comp_name, sf );
      if ( is_server )
        printf ( "<server>" );
      printf ( "%s%s%s", sf, user_name, sf );
    }
  else
    {
      printf ( "%-17s%-17s", inet_ntoa ( addr ), comp_name );
      if ( is_server )
        printf ( "%-10s", "<server>" );
      else
        printf ( "%-10s", "" );
      printf ( "%-17s", user_name );
    }
  if ( hostinfo->footer )
    {
      printf ( "%02x:%02x:%02x:%02x:%02x:%02x\n",
               hostinfo->footer->adapter_address[0],
               hostinfo->footer->adapter_address[1],
               hostinfo->footer->adapter_address[2],
               hostinfo->footer->adapter_address[3],
               hostinfo->footer->adapter_address[4],
               hostinfo->footer->adapter_address[5] );
    }
  else
    {
      printf ( "\n" );
    }
  return 1;
}

/* Print hostinfo in /etc/hosts or lmhosts format */
/* If l is true adds #PRE to each line of output (for lmhosts) */

static void
l_print_hostinfo ( struct in_addr addr, struct nb_host_info *hostinfo, int l )
{
  int i;
  unsigned char service; /* 16th byte of NetBIOS name */
  char comp_name[16];
  int unique;
  int first_name = 1;

  strncpy ( comp_name, "<unknown>", 15 );

  if ( hostinfo->header && hostinfo->names )
    {
      for ( i = 0; i < hostinfo->header->number_of_names; i++ )
        {
          service = hostinfo->names[i].ascii_name[15];
          unique = !( hostinfo->names[i].rr_flags & 0x0080 );
          if ( service == 0 && unique && first_name )
            {
              /* Unique name, workstation service - this is computer name */
              strncpy ( comp_name, hostinfo->names[i].ascii_name, 15 );
              comp_name[15] = 0;
              first_name = 0;
            }
        }
    }
  printf ( "%s\t%s", inet_ntoa ( addr ), comp_name );
  if ( l )
    printf ( "\t#PRE" );
  printf ( "\n" );
}

#define BUFFSIZE 1024

int
main ( int argc, char *argv[] )
{
  int timeout = 1000, verbose = 0, use137 = 0, ch, dump = 0, bandwidth = 0,
      send_ok = 0, hr = 0, etc_hosts = 0, lmhosts = 0;
  extern char *optarg;
  extern int optind;
  char *target_string, *temp_target_string = NULL;
  char *sf = NULL;
  char *filename = NULL;
  struct ip_range range;
  void *buff;
  int sock;
  socklen_t addr_size;
  struct sockaddr_in src_sockaddr, dest_sockaddr;
  struct in_addr *prev_in_addr = NULL;
  struct in_addr *next_in_addr;
  struct timeval select_timeout, last_send_time, current_time, diff_time,
          send_interval;
  struct timeval transmit_started, now, recv_time;
  struct nb_host_info *hostinfo;
  fd_set fdsr;
  fd_set fdsw;
  int size;
  struct list *scanned;
  my_uint32_t
          rtt_base; /* Base time (seconds) for round trip time calculations */
  float rtt;        /* most recent measured RTT, seconds */
  float srtt = 0;   /* smoothed rtt estimator, seconds */
  float rttvar = 0.75; /* smoothed mean deviation, seconds */
  double delta;        /* used in retransmit timeout calculations */
  int rto, retransmits = 0, more_to_send = 1, i;
  char errmsg[80];
  char str[80];
  FILE *targetlist = NULL;

  /* Parse supplied options */
  /**************************/
  if ( argc < 2 )
    {
      print_banner ();
      usage ();
    }

  while ( ( ch = getopt ( argc, argv, "vrdelqhm:s:t:b:f:" ) ) != -1 )
    switch ( ch )
      {
        case 'v':
          verbose = 1;
          break;
        case 't':
          timeout = atoi ( optarg );
          if ( timeout == 0 )
            {
              printf ( "Bad timeout value: %s\n", optarg );
              usage ();
            }
          break;
        case 'r':
#if defined WINDOWS
          printf ( "Warning: -r option not supported under Windows. Running "
                   "without it.\n\n" );
#else
          use137 = 1;
#endif
          break;
        case 'd':
          dump = 1;
          break;
        case 'e':
          etc_hosts = 1;
          break;
        case 'l':
          lmhosts = 1;
          break;
        case 'q':
          quiet = 1; /* Global variable */
          break;
        case 'b':
          bandwidth = atoi ( optarg );
          if ( bandwidth == 0 )
            err_print ( "Bad bandwidth value, ignoring it", quiet );
          break;
        case 'h':
          hr = 1; /* human readable service names instead of hex codes */
          break;
        case 's':
          sf = optarg; /* script-friendly output format */
          break;
        case 'm':
          retransmits = atoi ( optarg );
          if ( retransmits == 0 )
            {
              printf ( "Bad number of retransmits: %s\n", optarg );
              usage ();
            }
          break;
        case 'f':
          filename = optarg;
          break;
        default:
          print_banner ();
          usage ();
      }

  if ( dump && verbose )
    {
      printf ( "Cannot be used with both dump (-d) and verbose (-v) "
               "options.\n" );
      usage ();
    }

  if ( dump && sf )
    {
      printf ( "Cannot be used with both dump (-d) and script-friendly (-s) "
               "options.\n" );
      usage ();
    }

  if ( dump && lmhosts )
    {
      printf ( "Cannot be used with both dump (-d) and lmhosts (-l) "
               "options.\n" );
      usage ();
    }

  if ( dump && etc_hosts )
    {
      printf ( "Cannot be used with both dump (-d) and /etc/hosts (-e) "
               "options.\n" );
      usage ();
    }

  if ( verbose && lmhosts )
    {
      printf ( "Cannot be used with both verbose (-v) and lmhosts (-l) "
               "options.\n" );
      usage ();
    }

  if ( verbose && etc_hosts )
    {
      printf ( "Cannot be used with both verbose (-v) and /etc/hosts (-e) "
               "options.\n" );
      usage ();
    }

  if ( lmhosts && etc_hosts )
    {
      printf ( "Cannot be used with both lmhosts (-l) and /etc/hosts (-e) "
               "options.\n" );
      usage ();
    }

  if ( dump && hr )
    {
      printf ( "Cannot be used with both dump (-d) and \"human-readable "
               "service names\" (-h) options.\n" );
      usage ();
    }

  if ( hr && !verbose )
    {
      printf ( "\"Human-readable service names\" (-h) option cannot be used "
               "without verbose (-v) option.\n" );
      usage ();
    }

  if ( filename )
    {
      if ( strcmp ( filename, "-" ) == 0 )
        { /* Get IP addresses from stdin */
          targetlist = stdin;
          target_string = "STDIN";
        }
      else
        {
          targetlist = fopen ( filename, "r" );
          target_string = filename;
        }
      if ( !targetlist )
        {
          snprintf ( errmsg, 80, "Cannot open file %s", filename );
          err_die ( errmsg, quiet );
        }
    }
  else
    {
      argc -= optind;
      argv += optind;
      if ( argc != 1 )
        usage ();

      if ( ( target_string = strdup ( argv[0] ) ) == NULL )
        err_die ( "Malloc failed.\n", quiet );

      if ( !set_range ( target_string, &range ) )
        {
          printf ( "Error: %s is not an IP address or address range.\n",
                   target_string );
          free ( target_string );
          usage ();
        }

      temp_target_string = target_string;
    }

  if ( !( quiet || sf || lmhosts || etc_hosts ) )
    printf ( "Doing NBT name scan for addresses from %s\n\n", target_string );

  /* Finished with options */
  /*************************/

  /* Prepare socket and address structures */
  /*****************************************/
  sock = socket ( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
  if ( sock < 0 )
    err_die ( "Failed to create socket", quiet );

  memset ( &src_sockaddr, 0, sizeof src_sockaddr );
  src_sockaddr.sin_family = AF_INET;
  if ( use137 )
    src_sockaddr.sin_port = htons ( NB_DGRAM );
  if ( bind ( sock,
              ( struct sockaddr * ) &src_sockaddr,
              sizeof ( src_sockaddr ) ) == -1 )
    err_die ( "Failed to bind", quiet );

  FD_ZERO ( &fdsr );
  FD_SET ( sock, &fdsr );

  FD_ZERO ( &fdsw );
  FD_SET ( sock, &fdsw );

  /* timeout is in milliseconds */
  select_timeout.tv_sec = 60; /* Default 1 min to survive ARP timeouts */
  select_timeout.tv_usec = 0;

  addr_size = sizeof ( struct sockaddr_in );

  next_in_addr = malloc ( sizeof ( struct in_addr ) );
  if ( !next_in_addr )
    err_die ( "Malloc failed", quiet );

  buff = malloc ( BUFFSIZE );
  if ( !buff )
    err_die ( "Malloc failed", quiet );

  /* Calculate interval between subsequent sends */

  timerclear ( &send_interval );
  if ( bandwidth )
    send_interval.tv_usec =
            ( NBNAME_REQUEST_SIZE + UDP_HEADER_SIZE + IP_HEADER_SIZE ) * 8 *
            1000000 / bandwidth; /* Send interval in microseconds */
  else                           /* Assuming 10baseT bandwidth */
    send_interval.tv_usec = 1;   /* for 10baseT interval should be about 1 ms */
  if ( send_interval.tv_usec >= 1000000 )
    {
      send_interval.tv_sec = send_interval.tv_usec / 1000000;
      send_interval.tv_usec = send_interval.tv_usec % 1000000;
    }

  gettimeofday ( &last_send_time, NULL ); /* Get current time */

  rtt_base = last_send_time.tv_sec;

  /* Send queries, receive answers and print results */
  /***************************************************/

  scanned = new_list ();

  if ( !( quiet || verbose || dump || sf || lmhosts || etc_hosts ) )
    print_header ();

  for ( i = 0; i <= retransmits; i++ )
    {
      gettimeofday ( &transmit_started, NULL );
      while ( ( select ( sock + 1, &fdsr, &fdsw, NULL, &select_timeout ) ) > 0 )
        {
          if ( FD_ISSET ( sock, &fdsr ) )
            {
              if ( ( size = recvfrom ( sock,
                                       buff,
                                       BUFFSIZE,
                                       0,
                                       ( struct sockaddr * ) &dest_sockaddr,
                                       &addr_size ) ) <= 0 )
                {
                  snprintf ( errmsg,
                             80,
                             "%s\tRecvfrom failed",
                             inet_ntoa ( dest_sockaddr.sin_addr ) );
                  err_print ( errmsg, quiet );
                  continue;
                }
              gettimeofday ( &recv_time, NULL );
              hostinfo =
                      ( struct nb_host_info * ) parse_response ( buff, size );
              if ( !hostinfo )
                {
                  err_print ( "parse_response returned NULL", quiet );
                  continue;
                }
              /* If this packet isn't a duplicate */
              if ( insert ( scanned, ntohl ( dest_sockaddr.sin_addr.s_addr ) ) )
                {
                  rtt = recv_time.tv_sec + recv_time.tv_usec / 1000000 -
                        rtt_base - hostinfo->header->transaction_id / 1000;
                  /* Using algorithm described in Stevens'
                     Unix Network Programming */
                  delta = rtt - srtt;
                  srtt += delta / 8;
                  if ( delta < 0.0 )
                    delta = -delta;
                  rttvar += ( delta - rttvar ) / 4;

                  if ( verbose )
                    v_print_hostinfo (
                            dest_sockaddr.sin_addr, hostinfo, sf, hr );
                  else if ( dump )
                    d_print_hostinfo ( dest_sockaddr.sin_addr, hostinfo );
                  else if ( etc_hosts )
                    l_print_hostinfo ( dest_sockaddr.sin_addr, hostinfo, 0 );
                  else if ( lmhosts )
                    l_print_hostinfo ( dest_sockaddr.sin_addr, hostinfo, 1 );
                  else
                    print_hostinfo ( dest_sockaddr.sin_addr, hostinfo, sf );
                }

              free ( hostinfo->header );
              free ( hostinfo->footer );
              free ( hostinfo->names );
              free ( hostinfo );
            }

          FD_ZERO ( &fdsr );
          FD_SET ( sock, &fdsr );

          /* check if send_interval time passed since last send */
          gettimeofday ( &current_time, NULL );
          timersub ( &current_time, &last_send_time, &diff_time );
          send_ok = timercmp ( &diff_time, &send_interval, >= );

          if ( more_to_send && FD_ISSET ( sock, &fdsw ) && send_ok )
            {
              if ( targetlist )
                {
                  if ( fgets ( str, 80, targetlist ) )
                    {
                      if ( !inet_aton ( str, next_in_addr ) )
                        {
                          /* if(!inet_pton(AF_INET, str, next_in_addr)) { */
                          fprintf ( stderr, "%s - bad IP address\n", str );
                        }
                      else
                        {
                          if ( !in_list ( scanned,
                                          ntohl ( next_in_addr->s_addr ) ) )
                            send_query ( sock, *next_in_addr, rtt_base );
                        }
                    }
                  else
                    {
                      if ( feof ( targetlist ) )
                        {
                          more_to_send = 0;
                          FD_ZERO ( &fdsw );
                          /* timeout is in milliseconds */
                          select_timeout.tv_sec = timeout / 1000;
                          select_timeout.tv_usec =
                                  ( timeout % 1000 ) * 1000; /* Microseconds */
                          continue;
                        }
                      else
                        {
                          snprintf ( errmsg,
                                     80,
                                     "Read failed from file %s",
                                     filename );
                          err_die ( errmsg, quiet );
                        }
                    }
                }
              else if ( next_address ( &range, prev_in_addr, next_in_addr ) )
                {
                  if ( !in_list ( scanned, ntohl ( next_in_addr->s_addr ) ) )
                    send_query ( sock, *next_in_addr, rtt_base );
                  prev_in_addr = next_in_addr;
                  /* Update last send time */
                  gettimeofday ( &last_send_time, NULL );
                }
              else
                { /* No more queries to send */
                  more_to_send = 0;
                  FD_ZERO ( &fdsw );
                  /* timeout is in milliseconds */
                  select_timeout.tv_sec = timeout / 1000;
                  select_timeout.tv_usec =
                          ( timeout % 1000 ) * 1000; /* Microseconds */
                  continue;
                }
            }
          if ( more_to_send )
            {
              FD_ZERO ( &fdsw );
              FD_SET ( sock, &fdsw );
            }
        }

      if ( i >= retransmits )
        break; /* If we are not going to retransmit
             we can finish right now without waiting */

      rto = ( srtt + 4 * rttvar ) * ( i + 1 );

      if ( rto < 2.0 )
        rto = 2.0;
      if ( rto > 60.0 )
        rto = 60.0;
      gettimeofday ( &now, NULL );

      if ( now.tv_sec < ( transmit_started.tv_sec + rto ) )
        sleep ( ( transmit_started.tv_sec + rto ) - now.tv_sec );
      prev_in_addr = NULL;
      more_to_send = 1;
      FD_ZERO ( &fdsw );
      FD_SET ( sock, &fdsw );
      FD_ZERO ( &fdsr );
      FD_SET ( sock, &fdsr );
    }

  delete_list ( scanned );
  free ( next_in_addr );
  free ( temp_target_string );
  free ( buff );
  exit ( 0 );
}
