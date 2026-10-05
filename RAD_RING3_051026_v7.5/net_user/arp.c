/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/arp.c
 * DESCRIPTION: Processamento de requisições/respostas ARP e Cache de MACs
 * ============================================================================ */

#include "arp.h"
#include "net_interface.h"
#include "net_utils.h"

// ============================================================================
// VARIÁVEIS GLOBAIS
// ============================================================================
static arp_entry_t g_arp_table[ARP_TABLE_SIZE];
static uint32_t    g_my_ip = 0;
static uint8_t     g_next_victim = 0;

static inline void mac_copy(uint8_t* dest, const uint8_t* src) {
    for (int i = 0; i < 6; i++) {
        dest[i] = src[i];
    }
}

// ============================================================================
// FUNÇÕES PÚBLICAS E INTERNAS
// ============================================================================
void arp_init(uint32_t my_ip) {
    g_my_ip = my_ip;
    g_next_victim = 0;

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        g_arp_table[i].valid = false;
        g_arp_table[i].ip = 0;
    }
}

void arp_set_ip(uint32_t my_ip) {
    g_my_ip = my_ip;
}

uint32_t arp_get_ip(void) {
    return g_my_ip;
}

static void arp_cache_insert(uint32_t ip, const uint8_t mac[6]) {
    if (ip == 0 || !mac) return;

    // Atualiza se já existir na tabela
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip) {
            mac_copy(g_arp_table[i].mac, mac);
            return;
        }
    }

    // Insere em um slot livre
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!g_arp_table[i].valid) {
            g_arp_table[i].ip = ip;
            mac_copy(g_arp_table[i].mac, mac);
            g_arp_table[i].valid = true;
            return;
        }
    }

    // Substituição via FIFO/Round Robin se a tabela estiver cheia
    g_arp_table[g_next_victim].ip = ip;
    mac_copy(g_arp_table[g_next_victim].mac, mac);
    g_arp_table[g_next_victim].valid = true;
    g_next_victim = (g_next_victim + 1) % ARP_TABLE_SIZE;
}

bool arp_lookup(uint32_t ip, uint8_t out_mac[6]) {
    if (!out_mac) return false;

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip) {
            mac_copy(out_mac, g_arp_table[i].mac);
            return true;
        }
    }

    return false;
}

int arp_send_request(uint32_t target_ip) {
    arp_header_t packet;
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t my_mac[6];

    if (g_my_ip == 0) {
        g_my_ip = MAKE_IP(10, 0, 2, 15);
    }

    net_get_my_mac(my_mac);

    packet.htype = htons(ARP_HTYPE_ETHERNET);
    packet.ptype = htons(ARP_PTYPE_IPV4);
    packet.hlen  = 6;
    packet.plen  = 4;
    packet.opcode = htons(ARP_OP_REQUEST);
    packet.sender_ip = g_my_ip;
    packet.target_ip = target_ip;
    mac_copy(packet.sender_mac, my_mac);

    for (int i = 0; i < 6; i++) {
        packet.target_mac[i] = 0x00;
    }

    return net_send_frame(broadcast_mac, ETH_P_ARP, &packet, sizeof(arp_header_t));
}

void arp_process_packet(const uint8_t* buffer, uint16_t len) {
    if (!buffer || len < sizeof(arp_header_t)) return;

    const arp_header_t* arp = (const arp_header_t*)buffer;

    if (ntohs(arp->htype) != ARP_HTYPE_ETHERNET || ntohs(arp->ptype) != ARP_PTYPE_IPV4) {
        return;
    }

    uint16_t opcode = ntohs(arp->opcode);
    bool is_for_me = (g_my_ip == 0) || (arp->target_ip == g_my_ip);

    if (opcode == ARP_OP_REPLY || (opcode == ARP_OP_REQUEST && is_for_me)) {
        arp_cache_insert(arp->sender_ip, arp->sender_mac);
    }

    if (opcode == ARP_OP_REQUEST && is_for_me && g_my_ip != 0) {
        arp_header_t reply;
        uint8_t my_mac[6];

        net_get_my_mac(my_mac);

        reply.htype = htons(ARP_HTYPE_ETHERNET);
        reply.ptype = htons(ARP_PTYPE_IPV4);
        reply.hlen  = 6;
        reply.plen  = 4;
        reply.opcode = htons(ARP_OP_REPLY);
        reply.sender_ip = g_my_ip;
        reply.target_ip = arp->sender_ip;

        mac_copy(reply.sender_mac, my_mac);
        mac_copy(reply.target_mac, arp->sender_mac);

        net_send_frame(arp->sender_mac, ETH_P_ARP, &reply, sizeof(arp_header_t));
    }
}
