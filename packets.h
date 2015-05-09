// Copyright © 2015, The Network Protocol Company, Inc. All Rights Reserved.

#ifndef PACKETS_H
#define PACKETS_H

#include "protocol.h"
#include "game.h"

enum PacketType
{
    PACKET_TYPE_CONNECTION_REQUEST,
    PACKET_TYPE_CONNECTION_ACCEPTED,
    PACKET_TYPE_CONNECTION_DENIED,
    PACKET_TYPE_INPUT,
    PACKET_TYPE_SNAPSHOT,
    NUM_PACKET_TYPES
};

struct Packet
{
    uint32_t type;
};

struct ConnectionRequestPacket : public Packet
{
    uint64_t client_guid;

    SERIALIZE_OBJECT( stream )
    {
        serialize_uint64( stream, client_guid );
    }
};

struct ConnectionAcceptedPacket : public Packet
{
    uint64_t client_guid;

    SERIALIZE_OBJECT( stream )
    {
        serialize_uint64( stream, client_guid );
    }
};

struct ConnectionDeniedPacket : public Packet
{
    uint64_t client_guid;

    SERIALIZE_OBJECT( stream )
    {
        serialize_uint64( stream, client_guid );
    }
};

struct InputPacket : public Packet
{
    bool synchronizing;
    uint16_t sync_offset;
    uint16_t sync_sequence;
    uint64_t tick;
    int num_inputs;
    Input input[MaxInputsPerPacket];

    SERIALIZE_OBJECT( stream )
    {
        serialize_bool( stream, synchronizing );
        if ( synchronizing )
        {
            serialize_uint16( stream, sync_offset );
            serialize_uint16( stream, sync_sequence );
            serialize_uint64( stream, tick );
        }
        else
        {
            serialize_uint64( stream, tick );
            serialize_int( stream, num_inputs, 0, MaxInputsPerPacket );
            for ( int i = 0; i < num_inputs; ++i )
            {
                serialize_bool( stream, input[i].left );
                serialize_bool( stream, input[i].right );
                serialize_bool( stream, input[i].up );
                serialize_bool( stream, input[i].down );
                serialize_bool( stream, input[i].push );
                serialize_bool( stream, input[i].pull );
            }
        }
    }
};

struct SnapshotPacket : public Packet
{
    bool synchronizing;
    uint16_t sync_offset;
    uint16_t sync_sequence;
    uint64_t tick;

    SERIALIZE_OBJECT( stream )
    {
        serialize_bool( stream, synchronizing );
        if ( synchronizing )
        {
            serialize_uint64( stream, tick );
            serialize_uint16( stream, sync_offset );
            serialize_uint16( stream, sync_sequence );
        }
        else
        {
            serialize_uint64( stream, tick );
            // todo: serialize rest of snapshot
        }
    }
};

bool write_packet( WriteStream & stream, Packet & base_packet, int & packet_bytes )
{
    typedef WriteStream Stream;
    serialize_int( stream, base_packet.type, 0, NUM_PACKET_TYPES - 1 );
    switch ( base_packet.type )
    {
        // todo: convert these to macros

        case PACKET_TYPE_CONNECTION_REQUEST:
        {
            ConnectionRequestPacket & packet = (ConnectionRequestPacket&) base_packet;
            serialize_object( stream, packet );
        }
        break;

        case PACKET_TYPE_CONNECTION_ACCEPTED:
        {
            ConnectionAcceptedPacket & packet = (ConnectionAcceptedPacket&) base_packet;
            serialize_object( stream, packet );
        }
        break;

        case PACKET_TYPE_CONNECTION_DENIED:
        {
            ConnectionDeniedPacket & packet = (ConnectionDeniedPacket&) base_packet;
            serialize_object( stream, packet );
        }
        break;

        case PACKET_TYPE_INPUT:
        {
            InputPacket & packet = (InputPacket&) base_packet;
            serialize_object( stream, packet );
        }
        break;

        case PACKET_TYPE_SNAPSHOT:
        {
            SnapshotPacket & packet = (SnapshotPacket&) base_packet;
            serialize_object( stream, packet );
        }
        break;
    }
    stream.Flush();
    packet_bytes = stream.GetBytesProcessed();
    return !stream.IsOverflow();
}

extern bool process_packet( const class Address & from, Packet & packet, void * context );

bool read_packet( const class Address & from, uint8_t * buffer, int buffer_size, void * context )
{
    typedef ReadStream Stream;
    int packet_type;
    ReadStream stream( buffer, buffer_size );
    serialize_int( stream, packet_type, 0, NUM_PACKET_TYPES - 1 );
    switch( packet_type )
    {
        // todo: convert these to macros

        case PACKET_TYPE_CONNECTION_REQUEST:
        {
            ConnectionRequestPacket packet;
            packet.type = packet_type;
            serialize_object( stream, packet );
            if ( !stream.IsOverflow() )
                return process_packet( from, packet, context );
        }
        break;

        case PACKET_TYPE_CONNECTION_ACCEPTED:
        {
            ConnectionAcceptedPacket packet;
            packet.type = packet_type;
            serialize_object( stream, packet );
            if ( !stream.IsOverflow() )
                return process_packet( from, packet, context );
        }
        break;

        case PACKET_TYPE_CONNECTION_DENIED:
        {
            ConnectionDeniedPacket packet;
            packet.type = packet_type;
            serialize_object( stream, packet );
            if ( !stream.IsOverflow() )
                return process_packet( from, packet, context );
        }
        break;

        case PACKET_TYPE_INPUT:
        {
            InputPacket packet;
            packet.type = packet_type;
            serialize_object( stream, packet );
            if ( !stream.IsOverflow() )
                return process_packet( from, packet, context );
        }
        break;

        case PACKET_TYPE_SNAPSHOT:
        {
            SnapshotPacket packet;
            packet.type = packet_type;
            serialize_object( stream, packet );
            if ( !stream.IsOverflow() )
                return process_packet( from, packet, context );
        }
        break;
    }

    return false;
}

const char * packet_type_string( int packet_type )
{
    switch ( packet_type )
    {
        case PACKET_TYPE_CONNECTION_REQUEST:                return "connection request";
        case PACKET_TYPE_CONNECTION_ACCEPTED:               return "connection accepted";
        case PACKET_TYPE_CONNECTION_DENIED:                 return "connection denied";
        case PACKET_TYPE_INPUT:                             return "input";
        case PACKET_TYPE_SNAPSHOT:                          return "snapshot";
        default:
            assert( false );
            return "???";
    }
}

#endif // #ifndef PACKETS_H