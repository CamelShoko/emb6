/*
 * thrd-adv.c
 *
 *  Created on: 10 May 2016
 *  Author: Lukas Zimmermann <lzimmer1@stud.hs-offenburg.de>
 *  MLE Advertisement Processing / Route64 TLV Generation.
 */

#include "thrd-adv.h"

#include "emb6.h"
#include "net_tlv.h"
#include "thrd-route.h"
#include "tlv.h"
#include "thrd-partition.h"

/*==============================================================================
                          LOCAL VARIABLE DECLARATIONS
 =============================================================================*/

static uint8_t route64_tlv[MAX_ROUTE64_TLV_DATA_SIZE];

/*==============================================================================
                               LOCAL FUNCTION PROTOTYPES
 =============================================================================*/

static void thrd_process_route64(uint8_t rid_sender, tlv_route64_t *route64_tlv);
static uint8_t thrd_get_id_seq_number(tlv_route64_t *route64_tlv);

/*==============================================================================
                                    LOCAL FUNCTIONS
 =============================================================================*/

/* -------------------------------------------------------------------------- */

static void
thrd_process_route64(uint8_t rid_sender, tlv_route64_t *route64_tlv)
{
	thrd_rdb_link_t *link;

	if ( route64_tlv != NULL ) {

		if ( route64_tlv->id_seq > ID_sequence_number ) {

			// Empty the Router ID Set.
			thrd_rdb_rid_empty();

			ID_sequence_number = route64_tlv->id_seq;

			// printf("router_id_mask = %02x\n", router_id_mask);

			uint64_t bit_mask = 0x8000000000000000;
			uint8_t data_cnt = 0;

			PRINTF("thrd_process_route64: route64_tlv->router_id_msk = %lu\n", route64_tlv->router_id_mask);

			// Replace the ID Set.
			for ( uint8_t id_cnt = 0; id_cnt < 64; id_cnt++) {
				if ( (route64_tlv->router_id_mask & bit_mask) > 0 ) {

					thrd_rdb_rid_add(id_cnt);

					// Process Link Quality and Route Data.

					// Incoming quality.
					uint8_t lq_rd_data = (route64_tlv->lq[data_cnt] & 0x30) >> 6;
					if ( lq_rd_data != 0 ) {
						link = thrd_rdb_link_lookup(id_cnt);
						link->L_outgoing_quality = lq_rd_data;
					}
					// Route data.
					lq_rd_data = (route64_tlv->lq[data_cnt] & 0x0F);
					// TODO Check whether the destination differs to the current router id. (Otherwise, we would create a loop).
					thrd_rdb_route_update(rid_sender, id_cnt, lq_rd_data);

					// printf("update route: %d | %d | %d\n", id_cnt, rid_sender, lq_rd_data);

					data_cnt++;
				}
				bit_mask >>= 1;
			}
		}
	}
}

/* -------------------------------------------------------------------------- */

static uint8_t
thrd_get_id_seq_number(tlv_route64_t  *route64_tlv)
{
	return route64_tlv->id_seq;
}

/*==============================================================================
                                     API FUNCTIONS
 =============================================================================*/

void
thrd_process_adv(uint16_t source_addr, tlv_route64_t *route64_tlv, tlv_leader_t *leader_tlv)
{
	if ( thrd_partition_process(thrd_get_id_seq_number(route64_tlv), leader_tlv) ) {
		thrd_process_route64((source_addr & 0xFC00) >> 10, route64_tlv);
	}
}

/* -------------------------------------------------------------------------- */

tlv_t *
thrd_generate_route64()
{
	tlv_t *tlv;								// TLV structure.
	thrd_rdb_id_t *rid;						// Router IDs.
	thrd_rdb_link_t *link;
	thrd_rdb_route_t *route;				// Routing entries.
	uint8_t tlv_len = 5;					// TLV length (value).

	// TLV type.
	route64_tlv[0] = TLV_ROUTE64;

	// ID Sequence number.
	route64_tlv[2] = ID_sequence_number;

	rid = thrd_rdb_rid_head();

	// Router ID Mask and Link Quality and Router Data.
	uint32_t router_id_mask = 0;
	for ( rid = thrd_rdb_rid_head(); rid != NULL; rid = thrd_rdb_rid_next(rid) ) {

		uint8_t lq_rd = 0x00;	// Link Quality and Route Data.

		router_id_mask |= (0x80000000 >> rid->router_id);
		// Link Set entry.
		link = thrd_rdb_link_lookup(rid->router_id);
		if ( link != NULL ) {
			lq_rd |= (link->L_outgoing_quality << 6);
			lq_rd |= (link->L_incoming_quality << 4);
		}
		// Routing entry.
		route = thrd_rdb_route_lookup(rid->router_id);
		if ( route != NULL ) {
			lq_rd |= (route->R_route_cost);
		}
		route64_tlv[tlv_len + 2] = lq_rd;
		tlv_len++;
	}

	route64_tlv[6] = (uint8_t) router_id_mask;
	for ( uint8_t i = 5; i > 2; i-- ) {
		router_id_mask >>= 8;
		route64_tlv[i] = (uint8_t) router_id_mask;
	}

	// Add length.
	route64_tlv[1] = tlv_len;

	if ( tlv_init(&tlv, route64_tlv) == 0 )
		return NULL;

	return tlv;
}
