/* SPDX-FileCopyrightText: 2010-2021 Comcast Cable Communications Management, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE 1
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cimplog/cimplog.h>

#include "b64.h"
#include "msgpack.h"
#include "string.h"
#include "utils.h"
#include "wrp-c.h"

/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
#define LOGGING_MODULE   "WRP-C"

#define WRP_ERROR( ... ) cimplog_error(LOGGING_MODULE, __VA_ARGS__)
#define WRP_INFO( ... )  cimplog_info(LOGGING_MODULE, __VA_ARGS__)
#define WRP_DEBUG( ... ) cimplog_debug(LOGGING_MODULE, __VA_ARGS__)

/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
#define METADATA_MAP_SIZE   1


/*----------------------------------------------------------------------------*/
/*                             Function Prototypes                            */
/*----------------------------------------------------------------------------*/
static int alterMap( char * buf );
static void free_common_fields( headers_t *h, partners_t *p, data_t *metadata,
                                struct money_trace_spans *spans );
static void free_msg_type__req( struct wrp_req_msg *req );
static void free_msg_type__event( struct wrp_event_msg *event );
static void free_msg_type__svc_registration( struct wrp_svc_registration_msg *reg );
static void free_msg_type__crud( struct wrp_crud_msg *crud );


/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/

/* See wrp-c.h for details. */
ssize_t wrp_struct_to( const wrp_msg_t *msg, const enum wrp_format fmt, void **bytes )
{
    char *data = NULL;
    ssize_t rv;

    if( NULL == msg || NULL == bytes ) {
        return -1;
    }

    switch( fmt ) {
        case WRP_BYTES:
            rv = wrp_struct_to_bytes( msg, &data );
            break;

        case WRP_BASE64:
            rv = wrp_struct_to_base64( msg, &data );
            break;

        case WRP_STRING:
            rv = internal_struct_to_string( msg, &data );
            break;

        default:
            return -1;
    }

    *bytes = data;

    return rv;
}


/* See wrp-c.h for details. */
char* wrp_struct_to_string( const wrp_msg_t *msg )
{
    char *string = NULL;

    if( NULL != msg ) {
        internal_struct_to_string( msg, &string );
    }

    return string;
}

/* See wrp-c.h for details. */
ssize_t wrp_to_struct( const void *bytes, const size_t length,
                       const enum wrp_format fmt, wrp_msg_t **msg )
{
    ssize_t rv = -1;

    if( NULL == bytes || length <= 0 || NULL == msg ) {
        return -1;
    }

    switch( fmt ) {
        case WRP_BYTES:
            rv = wrp_bytes_to_struct( bytes, length, msg );
            break;

        case WRP_BASE64:
            rv = wrp_base64_to_struct( bytes, length, msg );
            break;

        default:
            return -1;
    }

    return rv;
}

/* See wrp-c.h for details. */
void wrp_free_struct( wrp_msg_t *msg )
{
    if( NULL == msg ) {
        return;
    }

    switch( msg->msg_type ) {
        case WRP_MSG_TYPE__REQ:
            free_msg_type__req( &msg->u.req );
            break;

        case WRP_MSG_TYPE__EVENT:
            free_msg_type__event( &msg->u.event );
            break;

        case WRP_MSG_TYPE__SVC_REGISTRATION:
            free_msg_type__svc_registration( &msg->u.reg );
            break;

        case WRP_MSG_TYPE__CREATE:
        case WRP_MSG_TYPE__RETREIVE:
        case WRP_MSG_TYPE__UPDATE:
        case WRP_MSG_TYPE__DELETE:
            free_msg_type__crud( &msg->u.crud );
            break;

        case WRP_MSG_TYPE__AUTH:
        case WRP_MSG_TYPE__SVC_ALIVE:
            break;

        default:
            WRP_ERROR( "wrp_free_struct()->Invalid Message Type! (0x%x)\n",
                       msg->msg_type );
            break;
    }

    free( msg );
}

/* See wrp-c.h for details. */
const char *wrp_get_msg_dest( const wrp_msg_t *wrp_msg )
{
    if( wrp_msg->msg_type == WRP_MSG_TYPE__REQ ) {
        return ( const char * )wrp_msg->u.req.dest;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__EVENT ) {
        return ( const char * )wrp_msg->u.event.dest;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__CREATE ) {
        return ( const char * )wrp_msg->u.crud.dest;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__RETREIVE ) {
        return ( const char * )wrp_msg->u.crud.dest;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__UPDATE ) {
        return ( const char * )wrp_msg->u.crud.dest;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__DELETE ) {
        return ( const char * )wrp_msg->u.crud.dest;
    }

    return NULL;
}

/* See wrp-c.h for details. */
const char *wrp_get_msg_source( const wrp_msg_t *wrp_msg )
{
    if( wrp_msg->msg_type == WRP_MSG_TYPE__REQ ) {
        return ( const char * )wrp_msg->u.req.source;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__EVENT ) {
        return ( const char * )wrp_msg->u.event.source;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__CREATE ) {
        return ( const char * )wrp_msg->u.crud.source;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__RETREIVE ) {
        return ( const char * )wrp_msg->u.crud.source;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__UPDATE ) {
        return ( const char * )wrp_msg->u.crud.source;
    }

    if( wrp_msg->msg_type == WRP_MSG_TYPE__DELETE ) {
        return ( const char * )wrp_msg->u.crud.source;
    }

    return NULL;
}

/* See wrp-c.h for details. */
char *wrp_get_msg_element( const enum wrp_device_id_element element,
                           const wrp_msg_t *wrp_msg, const enum wrp_token_name token )
{
    const char *dest;
    const char *source;
    const char *start = NULL, *end = NULL;
    char *rv = NULL;

    if( token == DEST ) {
        dest = wrp_get_msg_dest( wrp_msg );

        if( NULL != dest ) {
            start = dest;
        }
    } else if( token == SOURCE ) {
        source = wrp_get_msg_source( wrp_msg );

        if( NULL != source ) {
            start = source;
        }
    }

    if( NULL != start ) {
        end = strchr( start, ':' );

        if( NULL != end ) {
            if( WRP_ID_ELEMENT__SCHEME == element ) {
                rv = strdupptr( start, end );
            } else {
                start = end;
                start++;
                end = strchr( start, '/' );

                if( NULL != end ) {
                    if( WRP_ID_ELEMENT__ID == element ) {
                        rv = strdupptr( start, end );
                    } else {
                        start = end;
                        start++;
                        end = strchr( start, '/' );

                        if( NULL != end ) {
                            if( WRP_ID_ELEMENT__SERVICE == element ) {
                                rv = strdupptr( start, end );
                            } else {
                                if( WRP_ID_ELEMENT__APPLICATION == element ) {
                                    start = end;
                                    start++;

                                    if( 0 < strlen( start ) ) {
                                        rv = strdup( start );
                                    }
                                }
                            }
                        } else {
                            if( WRP_ID_ELEMENT__SERVICE == element ) {
                                if( 0 < strlen( start ) ) {
                                    rv = strdup( start );
                                }
                            }
                        }
                    }
                } else {
                    if( WRP_ID_ELEMENT__ID == element ) {
                        if( 0 < strlen( start ) ) {
                            rv = strdup( start );
                        }
                    }
                }
            }
        } else {
            if( WRP_ID_ELEMENT__SCHEME == element ) {
                if( 0 < strlen( start ) ) {
                    rv = strdup( start );
                }
            }
        }
    }

    return rv;
}

/* See wrp-c.h for details. */
int wrp_does_service_match( const char *service, const char *device_id )
{
    const char *p;

    p = strchr( device_id, '/' );

    if( ( NULL != p ) && ( '/' == *p ) ) {
        size_t len;
        p++;

        len = strlen( service );

        if( 0 == strncmp( p, service, len ) ) {
            p += len;

            if( ( '\0' == *p ) || ( '/' == *p ) ) {
                return 0;
            }
        }
    }

    return -1;
}


/*----------------------------------------------------------------------------*/
/*                             Internal functions                             */
/*----------------------------------------------------------------------------*/


/**
 * @brief alterMap function to change MAP size of encoded msgpack object.
 *
 * @param[in] encodedBuffer msgpack object
 * @param[out] return 0 in success or less than 1 in failure case
 */
static int alterMap( char * buf )
{
    //Extract 1st byte from binary stream which holds type and map size
    unsigned char *byte = ( unsigned char * )( &( buf[0] ) );
    int mapSize;
    WRP_DEBUG( "First byte in hex : %x\n", 0xff & *byte );
    //Calculate map size
    mapSize = ( 0xff & *byte ) % 0x10;
    WRP_DEBUG( "Map size is :%d\n", mapSize );

    if( mapSize == 15 ) {
        WRP_ERROR( "Msgpack Map (fixmap) is already at its MAX size i.e. 15\n" );
        return -1;
    }

    *byte = *byte + METADATA_MAP_SIZE;
    mapSize = ( 0xff & *byte ) % 0x10;
    WRP_DEBUG( "New Map size : %d\n", mapSize );
    //Update 1st byte with new MAP size
    buf[0] = *byte;
    return 0;
}


/**
 * @brief appendEncodedData function to append two encoded buffer and change MAP size accordingly.
 *
 * @note appendEncodedData function allocates memory for buffer, caller needs to free the buffer(appendData)in
 * both success or failure case. use wrp_free_struct() for free
 *
 * @param[in] encodedBuffer msgpack object (first buffer)
 * @param[in] encodedSize is size of first buffer
 * @param[in] metadataPack msgpack object (second buffer)
 * @param[in] metadataSize is size of second buffer
 * @param[out] appendData final encoded buffer after append
 * @return  appended total buffer size or less than 1 in failure case
 */
size_t appendEncodedData( void **appendData, void *encodedBuffer, size_t encodedSize,
                          void *metadataPack, size_t metadataSize )
{
    //Allocate size for final buffer
    *appendData = malloc( sizeof(char*) * (encodedSize + metadataSize) );

    if( *appendData != NULL ) {
        memcpy( *appendData, encodedBuffer, encodedSize );
        //Append 2nd encoded buf with 1st encoded buf
        memcpy( *appendData + ( encodedSize ), metadataPack, metadataSize );
        //Alter MAP
        int ret = alterMap( ( char * ) * appendData );

        if( ret ) {
            return -1;
        }

        return ( encodedSize + metadataSize );
    } else {
        WRP_ERROR( "Memory allocation failed\n" );
    }

    return -1;
}


static void free_common_fields( headers_t *h, partners_t *p, data_t *metadata,
                                struct money_trace_spans *spans )
{
    if( h ) {
        for( size_t i = 0; i < h->count; i++ ) {
            free( h->headers[i] );
        }
        free( h );
    }

    if( p ) {
        for( size_t i = 0; i < p->count; i++ ) {
            free( p->partner_ids[i] );
        }
        free( p );
    }

    if( metadata ) {
        for( size_t i = 0; i < metadata->count; i++ ) {
            free( metadata->data_items[i].name );
            free( metadata->data_items[i].value );
        }
        free( metadata->data_items );
        free( metadata );
    }

    if( spans ) {
        for( size_t i = 0; i < spans->count; i++ ) {
            free( spans->spans[i].name );
        }
        free( spans->spans );
    }
}


static void free_msg_type__req( struct wrp_req_msg *req )
{
    if( req->transaction_uuid ) free( req->transaction_uuid );
    if( req->content_type )     free( req->content_type );
    if( req->accept )           free( req->accept );
    if( req->source )           free( req->source );
    if( req->dest )             free( req->dest );
    if( req->payload )          free( req->payload );

    free_common_fields( req->headers, req->partner_ids, req->metadata, &req->spans );
}


static void free_msg_type__event( struct wrp_event_msg *event )
{
    if( event->content_type )     free( event->content_type );
    if( event->source )           free( event->source );
    if( event->dest )             free( event->dest );
    if( event->payload )          free( event->payload );

    free_common_fields( event->headers, event->partner_ids, event->metadata, NULL );
}


static void free_msg_type__svc_registration( struct wrp_svc_registration_msg *reg )
{
    if( reg->service_name ) free( reg->service_name );
    if( reg->url )          free( reg->url );
}


static void free_msg_type__crud( struct wrp_crud_msg *crud )
{
    if( crud->content_type )     free( crud->content_type );
    if( crud->accept )           free( crud->accept );
    if( crud->transaction_uuid ) free( crud->transaction_uuid );
    if( crud->source )           free( crud->source );
    if( crud->dest )             free( crud->dest );
    if( crud->path )             free( crud->path );
    if( crud->payload )          free( crud->payload );

    free_common_fields( crud->headers, crud->partner_ids, crud->metadata, &crud->spans );
}


