/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/net_poll.c
 * DESCRIPTION: Loop de recepção e despachante de quadros Ethernet
 * ============================================================================ */

#include "net_poll.h"
#include "net_interface.h"
#include "arp.h"
#include "ip.h"
#include "net_utils.h"

void net_poll(void) {
    uint8_t buffer[2048];
    uint16_t len = 0;

    // Tenta receber um frame da interface de rede
    if (net_poll_frame(buffer, &len) == 0) {
        // Quadros menores que o cabeçalho Ethernet são descartados
        if (len < ETH_HLEN) {
            return;
        }

        const ethernet_header_t *eth = (const ethernet_header_t *)buffer;
        uint16_t ethertype = ntohs(eth->ethertype);

        const uint8_t *payload = buffer + ETH_HLEN;
        uint16_t payload_len = len - ETH_HLEN;

        // Encaminha o pacote para o protocolo correspondente
        switch (ethertype) {
            case ETH_P_ARP:
                arp_process_packet(payload, payload_len);
                break;
            case ETH_P_IP:
                ip_process_packet(payload, payload_len);
                break;
            default:
                break;
        }
    }
}
