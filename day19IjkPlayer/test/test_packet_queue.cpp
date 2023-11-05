#include "ff_ffplay_def.h"
#include "log.h"

void create_queue(PacketQueue *packet_queue)
{
    // packet_queue_init(packet_queue); //abort_request 会赋值为1，导致packet_queue_put失败
    AVPacket *pkt1 = new AVPacket();
    pkt1->pts = 1;
    AVPacket *pkt2 = new AVPacket();
    pkt2->pts = 2;
    AVPacket *pkt3 = new AVPacket();
    pkt3->pts = 3;
    AVPacket *pkt4 = new AVPacket();
    pkt4->pts = 4;
    AVPacket *pkt5 = new AVPacket();
    pkt5->pts = 5;
    AVPacket *pkt6 = new AVPacket();
    pkt6->pts = 6;
    packet_queue_put(packet_queue, pkt1);
    packet_queue_put(packet_queue, pkt2);
    packet_queue_put(packet_queue, pkt3);
    packet_queue_put(packet_queue, pkt4);
    packet_queue_put(packet_queue, pkt5);
    packet_queue_put(packet_queue, pkt6);
    init_packet_queue_info(packet_queue);
}

int main()
{
    PacketQueue *packet_queue = new PacketQueue();

    // create queue
    create_queue(packet_queue);

    int serial;
    AVPacket *pkt = new AVPacket();
    packet_queue_get(packet_queue, pkt, 0, &serial);
    player_log_info(PACKET_QUEUE_TAG, packet_queue->print_to_string.c_str());
    packet_queue_get(packet_queue, pkt, 0, &serial);
    player_log_info(PACKET_QUEUE_TAG, packet_queue->print_to_string.c_str());

    // // flush queue
    packet_queue_flush(packet_queue);
    player_log_info(PACKET_QUEUE_TAG, packet_queue->print_to_string.c_str());

    // // destory queue
    // packet_queue_destroy(packet_queue);
    // print_packet_queue(packet_queue);
}
