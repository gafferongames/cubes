// Copyright © 2015, The Network Protocol Company, Inc. All Rights Reserved.

#include "platform.h"
#include "protocol.h"
#include "packets.h"
#include "network.h"
#include "game.h"
#include "world.h"
#include "render.h"
#include <stdio.h>

auto server_address = Address( "127.0.0.1", ServerPort );
//auto server_address = Address( "173.255.195.190", ServerPort );

#define HEADLESS 0
#define RUN_TESTS 0

#if !HEADLESS
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif // #if !HEADLESS

Render render;

Camera camera;

enum ClientState
{
    CLIENT_DISCONNECTED,
    CLIENT_SENDING_CONNECT_REQUEST,
    CLIENT_CONNECTION_DENIED,
    CLIENT_TIMED_OUT,
    CLIENT_CONNECTED
};

struct InputEntry
{
    uint64_t tick;
    Input input;
};

struct Client
{
    Socket * socket;
    uint64_t guid;
    ClientState state;
    Address server_address;
    uint16_t connect_sequence;
    
    double current_real_time;
    double time_last_packet_received;
    uint64_t client_tick;
    uint64_t server_tick;

    bool synchronizing;
    bool ready_to_apply_sync;
    bool synchronized;
    uint16_t sync_sequence;
    uint16_t sync_offset;

    bool bracketing;
    uint16_t bracket_offset;
    bool ready_to_apply_bracket_offset;
    bool bracketed;

    bool reconnect;
    int adjustment_offset;
    uint16_t adjustment_sequence;
    bool ready_to_apply_adjustment_offset;

    bool active;
    uint64_t input_ack;
    InputEntry inputs[InputSlidingWindowSize];

    bool suppress_send_packets;

    // hack: we just need this position of one cube for now -- eventually this will be a ring buffer of snapshots containing all world state
    vec3f cube_position;
    quat4f cube_orientation;
    vec3f cube_linear_velocity;
    vec3f cube_angular_velocity;
};

void client_init( Client & client )
{
    memset( &client, 0, sizeof( Client ) );
    client.socket = new Socket( 0 );
    client.guid = rand();
    client.state = CLIENT_DISCONNECTED;
    client.suppress_send_packets = false;

    // hack
    client.cube_position = vec3f(0,0,0);
    client.cube_orientation = quat4f::identity();
    client.cube_linear_velocity = vec3f(0,0,0);
    client.cube_angular_velocity = vec3f(0,0,0);
}

void client_connect( Client & client, const Address & address, double current_real_time )
{
    char buffer[256];
    printf( "client connecting to %s (%d)\n", address.ToString( buffer, sizeof( buffer ) ), client.connect_sequence + 1 );
    client.state = CLIENT_SENDING_CONNECT_REQUEST;
    client.server_address = address;
    client.client_tick = 0;
    client.server_tick = 0;
    client.current_real_time = current_real_time;
    client.time_last_packet_received = current_real_time;
    client.synchronizing = false;
    client.ready_to_apply_sync = false;
    client.synchronized = false;
    client.sync_sequence = 0;
    client.sync_offset = 0;
    client.bracketing = false;
    client.bracketed = false;
    client.bracket_offset = 0;
    client.ready_to_apply_bracket_offset = false;
    client.active = false;
    client.input_ack = 0;
    client.connect_sequence++;
    client.reconnect = false;
    client.adjustment_offset = 0;
    client.adjustment_sequence = 0;
    client.ready_to_apply_adjustment_offset = false;
    memset( client.inputs, 0, sizeof( client.inputs ) );
}

void client_reconnect( Client & client, double current_real_time )
{
    printf( "client reconnecting\n" );
    Address address = client.server_address;
    client_connect( client, address, current_real_time );
}

void client_disconnect( Client & client )
{
    client.state = CLIENT_DISCONNECTED;
}

void client_update( Client & client, double current_real_time )
{
    client.current_real_time = current_real_time;
    if ( client.state != CLIENT_DISCONNECTED && client.state != CLIENT_TIMED_OUT )
    {
        if ( current_real_time > client.time_last_packet_received + Timeout )
        {
            printf( "client timed out (%d)\n", client.connect_sequence );
            client.state = CLIENT_TIMED_OUT;
        }
    }
}

void client_apply_snapshot( Client & client, World & world )
{
    if ( !client.active )
        return;

    /*
    printf( "cube_position = (%f,%f,%f)\n", client.cube_position.x(), client.cube_position.y(), client.cube_position.z() );
    printf( "cube_orientation = (%f,%f,%f,%f)\n", client.cube_orientation.x(), client.cube_orientation.y(), client.cube_orientation.z(), client.cube_orientation.w() );
    printf( "cube_linear_velocity = (%f,%f,%f)\n", client.cube_linear_velocity.x(), client.cube_linear_velocity.y(), client.cube_linear_velocity.z() );
    printf( "cube_angular_velocity = (%f,%f,%f)\n", client.cube_angular_velocity.x(), client.cube_angular_velocity.y(), client.cube_angular_velocity.z() );
    */

    world.cube_manager->SetCubeState( 0, client.cube_position, client.cube_orientation, client.cube_linear_velocity, client.cube_angular_velocity );
}

void client_add_input( Client & client, const Input & input, uint64_t tick, int num_inputs )
{
    for ( int i = 0; i < num_inputs; ++i )
    {
        const int index = ( ( tick + i ) % InputSlidingWindowSize );
        client.inputs[index].tick = tick + i;
        client.inputs[index].input = input;
//        printf( "client add input %d\n", (int) client.inputs[index].tick );
    }
}

void client_send_packet( Client & client, Packet & packet )
{
    uint8_t buffer[MaxPacketSize];
    int packet_bytes = 0;
    WriteStream stream( buffer, MaxPacketSize );
    if ( write_packet( stream, packet, packet_bytes ) )
    {
        if ( client.socket->SendPacket( client.server_address, buffer, packet_bytes ) )
        {
            /*
            char address_buffer[1024];
            printf( "sent %s packet to server %s\n", packet_type_string( packet.type ), client.server_address.ToString( address_buffer, sizeof( address_buffer ) ) );
            */
        }
    }
}

void client_send_packets( Client & client )
{
    if ( client.suppress_send_packets )
    {
//        printf( "suppress send packets\n" );
        return;
    }

    switch ( client.state )
    {
        case CLIENT_SENDING_CONNECT_REQUEST:
        {
            ConnectionRequestPacket packet;
            packet.type = PACKET_TYPE_CONNECTION_REQUEST;
            packet.client_guid = client.guid;
            packet.connect_sequence = client.connect_sequence;
            client_send_packet( client, packet );
        }
        break;

        case CLIENT_CONNECTED:
        {
            InputPacket packet;
            packet.type = PACKET_TYPE_INPUT;
            packet.synchronizing = client.synchronizing;
            if ( client.synchronizing )
            {
                packet.sync_offset = client.sync_offset;
                packet.sync_sequence = client.sync_sequence;
                packet.tick = client.server_tick;
            }
            else
            {
                packet.tick = client.client_tick + TicksPerClientFrame - 1;
                packet.adjustment_sequence = client.adjustment_sequence;
                packet.bracketed = client.bracketed;
                packet.num_inputs = 0;
                for ( int i = 0; i < MaxInputsPerPacket; ++i )
                {
                    const int input_tick = packet.tick - i;
                    const int index = input_tick % InputSlidingWindowSize;
                    if ( client.inputs[index].tick != input_tick || input_tick == client.input_ack )
                        break;
                    packet.input[i] = client.inputs[index].input;
                    packet.num_inputs++;
                }
                /*
                if ( client.synchronized )
                    printf( "client sent %d inputs\n", packet.num_inputs );
                    */
            }
            client_send_packet( client, packet );
        }
        break;

        default:
            break;
    }
}

bool process_packet( const Address & from, Packet & base_packet, void * context )
{
    Client & client = *(Client*)context;

    switch ( base_packet.type )
    {
        case PACKET_TYPE_CONNECTION_ACCEPTED:
        {
            ConnectionAcceptedPacket & packet = (ConnectionAcceptedPacket&) base_packet;
            if ( client.state == CLIENT_SENDING_CONNECT_REQUEST &&
                 packet.client_guid == client.guid && packet.connect_sequence == client.connect_sequence )
            {
                printf( "client connected (%d)\n", client.connect_sequence );
                client.state = CLIENT_CONNECTED;
                return true;
            }
        }
        break;

        case PACKET_TYPE_CONNECTION_DENIED:
        {
            ConnectionDeniedPacket & packet = (ConnectionDeniedPacket&) base_packet;
            if ( client.state == CLIENT_SENDING_CONNECT_REQUEST &&
                 packet.client_guid == client.guid && packet.connect_sequence == client.connect_sequence )
            {
                printf( "client connection denied (%d)\n", client.connect_sequence );
                client.state = CLIENT_CONNECTION_DENIED;
                return true;
            }
        }
        break;

        case PACKET_TYPE_SNAPSHOT:
        {
            SnapshotPacket & packet = (SnapshotPacket&) base_packet;            
            if ( client.state == CLIENT_CONNECTED && packet.tick > client.server_tick )
            {
                if ( !client.synchronizing && packet.synchronizing )
                {
                    printf( "client synchronizing\n" );
                    client.synchronizing = true;
                }
                
                if ( client.synchronizing )
                {
                    if ( !packet.synchronizing && !client.ready_to_apply_sync )
                    {
                        client.ready_to_apply_sync = true;
                    }
                    else
                    {
                        client.server_tick = packet.tick;
                        client.sync_offset = packet.sync_offset;
                    }
                }
                else
                {
                    if ( !packet.synchronizing )
                    {
                        if ( client.bracketing && !packet.bracketing )
                        {
                            client.ready_to_apply_bracket_offset = true;
                            client.bracket_offset = packet.bracket_offset;
                        }

                        client.reconnect = packet.reconnect;
                        client.bracketing = packet.bracketing;
                        client.input_ack = packet.input_ack;
                        client.server_tick = packet.tick;

                        if ( client.adjustment_sequence != packet.adjustment_sequence ) // && packet.adjustment_offset != 0 )
                        {
                            client.adjustment_sequence = packet.adjustment_sequence;
                            client.adjustment_offset = packet.adjustment_offset;
                            client.ready_to_apply_adjustment_offset = true;
                        }

                        client.cube_position = packet.cube_position;
                        client.cube_orientation = packet.cube_orientation;
                        client.cube_linear_velocity = packet.cube_linear_velocity;
                        client.cube_angular_velocity = packet.cube_angular_velocity;
                    }
                }
            }
            return true;
        }
        break;

        default:
            break;
    }

    return false;
}

void client_receive_packets( Client & client )
{
    uint8_t buffer[MaxPacketSize];

    while ( true )
    {
        Address from;
        int bytes_read = client.socket->ReceivePacket( from, buffer, sizeof( buffer ) );
        if ( bytes_read == 0 )
            break;

        if ( read_packet( from, buffer, bytes_read, &client ) )
            client.time_last_packet_received = client.current_real_time;
    }
}

void client_apply_time_synchronization( Client & client, World & world )
{
    if ( client.synchronizing && client.ready_to_apply_sync )
    {
        printf( "client synchronized [+%d]\n", (int) client.sync_offset );
        world.tick = client.server_tick + client.sync_offset;
        client.client_tick = world.tick;
        client.synchronizing = false;
        client.ready_to_apply_sync = false;
        client.synchronized = true;
    }

    if ( client.ready_to_apply_bracket_offset )
    {
        printf( "client bracketed [-%d]\n", (int) client.bracket_offset );
        client.ready_to_apply_bracket_offset = false;
        client.bracketed = true;

        printf( "*** client is active ***\n" );
        client.active = true;
        client.input_ack = 0;
        world.tick -= client.bracket_offset;
        client.client_tick = world.tick;
        memset( client.inputs, 0, sizeof( client.inputs ) );
    }

    if ( client.ready_to_apply_adjustment_offset )
    {
        // todo: this is a hack to test it -- in reality what will be done is that this adjustment
        // will be made over the course of one second the client will tick run slightly more or less
        // ticks than usual

        printf( "client adjustment [%+d]. client side prediction = %d ticks\n", (int) client.adjustment_offset, (int) ( client.client_tick - client.server_tick ) );

        const uint64_t original_world_tick = world.tick;

        client.ready_to_apply_adjustment_offset = false;
        world.tick += client.adjustment_offset;
        client.client_tick = world.tick;

        // hack: need to back fill inputs for any frames we skip so server doesn't think we're dropping inputs
        if ( client.adjustment_offset > 0 )
            client_add_input( client, Input(), original_world_tick, client.adjustment_offset );
    }

    world.active = client.active;
}

void client_free( Client & client )
{
    delete client.socket;
    client = Client();    
}

struct Global
{
    int display_width;
    int display_height;
};

Global global;

void client_tick( World & world, const Input & input )
{
//    printf( "%d-%d: %f [%+.4f]\n", (int) world.frame, (int) world.tick, world.time, TickDeltaTime );

    const int player_id = 0;

    game_process_player_input( world, input, player_id );

    world_tick( world );
}

void client_frame( World & world, const Input & input, double real_time, double frame_time )
{
    for ( int i = 0; i < TicksPerClientFrame; ++i )
        client_tick( world, input );

    world.frame++;
}

void client_post_frame( Client & client, World & world )
{
    client.client_tick = world.tick;
}

#if HEADLESS

static volatile int quit = 0;

void interrupt_handler( int dummy )
{
    quit = 1;
}

int client_main( int argc, char ** argv )
{
    InitializeNetwork();

    Client client;

    client_init( client );

    World world;
    world_init( world );
    world_setup_cubes( world );
    world_tick( world );

    signal( SIGINT, interrupt_handler );

    client_connect( client, server_address, 0.0 );

    const double start_time = platform_time();

    double previous_frame_time = start_time;
    double next_frame_time = previous_frame_time + ClientFrameDeltaTime;

    while ( !quit )
    {
        if ( client.state == CLIENT_TIMED_OUT || client.state == CLIENT_CONNECTION_DENIED )
            break;

        const double time_to_sleep = max( 0.0, next_frame_time - platform_time() - AverageSleepJitter );

        platform_sleep( time_to_sleep );

        const double frame_time = next_frame_time;

        Input input;

        client_update( client, frame_time );

        client_receive_packets( client );

        client_apply_time_synchronization( client, world );

        client_add_input( client, input, world.tick, TicksPerClientFrame );

        client_send_packets( client );

        client_frame( world, input, frame_time, world.frame * ClientFrameDeltaTime );

        client_post_frame( client, world );

        client_apply_snapshot( client, world );

        const double end_of_frame_time = platform_time();

        int num_frames_advanced = 0;
        while ( next_frame_time < end_of_frame_time + ClientFrameSafety * ClientFrameDeltaTime )
        {
            next_frame_time += ClientFrameDeltaTime;
            num_frames_advanced++;
        }

        const int num_dropped_frames = max( 0, num_frames_advanced - 1 );

        if ( num_dropped_frames > 0 && client.state == CLIENT_CONNECTED && !client.synchronizing )
        {
            printf( "dropped frame %d (%d)\n", (int) world.frame, num_dropped_frames );
        }

        previous_frame_time = next_frame_time - ClientFrameDeltaTime;

        world.frame++;

#if RUN_TESTS

        if ( world.frame == 10 )
            client.reconnect = true;

        if ( ( world.frame > 500 && world.frame < 600 ) || ( world.frame > 1000 && world.frame < 1010 ) )
            client.suppress_send_packets = true;
        else
            client.suppress_send_packets = false;

#endif // #if RUN_TESTS

        if ( client.reconnect )
        {
            client_reconnect( client, platform_time() );
        }
    }

    printf( "\n" );

    world_free( world );

    client_free( client );

    ShutdownNetwork();

    return 0;
}

#else // #if HEADLESS

void framebuffer_size_callback( GLFWwindow * window, int width, int height )
{
    global.display_width = width;
    global.display_height = height;
}

void client_clear()
{
    glViewport( 0, 0, global.display_width, global.display_height );
    glClearStencil( 0 );
    glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );     
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
}

void client_render( const World & world, bool active )
{
    client_clear();

    if ( !active )
        return;

    CubeEntity * player = (CubeEntity*) world.entity_manager->GetEntity( ENTITY_PLAYER_BEGIN );

    vec3f origin = player ? player->position : vec3f(0,0,0);

    vec3f lookat = origin - vec3f(0,0,1);

    vec3f position = lookat + vec3f(0,-11,5);

    camera.EaseIn( lookat, position );

    RenderState render_state;
    render_get_state( world, render_state );

    render.ResizeDisplay( global.display_width, global.display_height );

    render.BeginScene( 0, 0, global.display_width, global.display_height );

    render.SetCamera( camera.position, camera.lookat, camera.up );
    
    render.SetLightPosition( camera.lookat + vectorial::vec3f( 25.0f, -50.0f, 100.0f ) );

    render.RenderScene( render_state );
    
    render.RenderShadows( render_state );

    render.RenderShadowQuad();
    
    render.EndScene();
}

Input client_sample_input( GLFWwindow * window )
{
    assert( window );
    Input input;
    input.left = glfwGetKey( window, GLFW_KEY_LEFT );
    input.right = glfwGetKey( window, GLFW_KEY_RIGHT );
    input.up = glfwGetKey( window, GLFW_KEY_UP );
    input.down = glfwGetKey( window, GLFW_KEY_DOWN );
    input.push = glfwGetKey( window, GLFW_KEY_SPACE );
    input.pull = glfwGetKey( window, GLFW_KEY_Z );
    return input;
}

int client_main( int argc, char ** argv )
{
    InitializeNetwork();

    Client client;

    client_init( client );

    World world;
    world_init( world );
    world_setup_cubes( world );
    world_tick( world );

    glfwInit();

    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 4 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 1 );
    glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );
    glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE );
    glfwWindowHint( GLFW_SRGB_CAPABLE, GL_TRUE );
    glfwWindowHint( GLFW_RESIZABLE, GL_FALSE );
    glfwWindowHint( GLFW_SAMPLES, 8 );
    glfwWindowHint( GLFW_STENCIL_BITS, 8 );
    glfwWindowHint( GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE );

    const GLFWvidmode * mode = glfwGetVideoMode( glfwGetPrimaryMonitor() );

    GLFWwindow * window = nullptr;

    bool fullscreen = false;

    if ( fullscreen )
        window = glfwCreateWindow( mode->width, mode->height, "Client", glfwGetPrimaryMonitor(), nullptr );
    else
        window = glfwCreateWindow( 1000, 500, "Client", nullptr, nullptr );

    if ( !window )
    {
        printf( "error: failed to create window\n" );
        exit( 1 );
    }

    int window_width, window_height;
    glfwGetWindowSize( window, &window_width, &window_height );

    const GLFWvidmode * desktop_mode = glfwGetVideoMode( glfwGetPrimaryMonitor() );
    if ( !desktop_mode )
    {
        printf( "error: desktop mode is null!\n" );
        return 1;
    }

    const int desktop_width = desktop_mode->width;
    const int desktop_height = desktop_mode->height;

    glfwSetWindowPos( window, desktop_width / 2 - window_width / 2, desktop_height / 2 - window_height / 2 );

    glfwGetFramebufferSize( window, &global.display_width, &global.display_height );

    glfwSetFramebufferSizeCallback( window, framebuffer_size_callback );

    glfwMakeContextCurrent( window );

    glewExperimental = GL_TRUE;
    glewInit();

    clear_opengl_error();

    client_clear();

    glEnable( GL_FRAMEBUFFER_SRGB );

    glEnable( GL_CULL_FACE );
    glFrontFace( GL_CW );

    double previous_frame_time = platform_time();

    client_connect( client, server_address, 0.0 );

    while ( true )
    {
        glfwSwapBuffers( window );

        double frame_start_time = platform_time();

        previous_frame_time = frame_start_time;

        Input input = client_sample_input( window );

        client_add_input( client, input, world.tick, TicksPerClientFrame );

        client_update( client, frame_start_time );

        client_receive_packets( client );

        client_apply_time_synchronization( client, world );

        client_send_packets( client );

        client_frame( world, input, frame_start_time, world.frame * ClientFrameDeltaTime );

        client_post_frame( client, world );

        client_apply_snapshot( client, world );

        client_render( world, client.active );

        glfwPollEvents();

        if ( glfwWindowShouldClose( window ) )
            break;

        if ( client.reconnect )
        {
            client_reconnect( client, platform_time() );
        }
    }

    world_free( world );

    client_free( client );

    glfwTerminate();

    ShutdownNetwork();

    return 0;
}

#endif // #if HEADLESS

int main( int argc, char ** argv )
{
    srand( time( NULL ) );

    client_main( argc, argv );

    return 0;
}
