/* SPDX */

#include "usteer.h"
#include <math.h>

static uint16_t usteer_estimate_throughput(struct sta *sta, struct usteer_node *node, int snr)
{
	int full_snr = 40;
	int base_tpt = 400;
	float band_penalty = 1.0;
	uint16_t tpt;

	/**
	 * ToDo: Estimate Bandwith based on AP & STA capabilities.
	 */

	if (node->freq < 3000)
		band_penalty = 0.6;

	tpt = base_tpt;

	/* Estimate Throughput based on SNR */
	if (snr < full_snr)
		tpt = tpt * ((float)snr / (float) full_snr);

	/* Estimate load effect */
	tpt = tpt * (100 - node->load) * 0.01;

	/* Add band penalty */
	tpt = tpt * band_penalty;

	return tpt;
}

static uint16_t usteer_score_candidate(struct sta_info *si, struct usteer_candidate *candidate)
{
	uint16_t score = candidate->estimated_throughput;
	uint64_t information_age;
	double age_penalty;

	/**
	 * Start with throughput as a baseline. This already takes the load / signal into
	 * consideration.
	 *
	 * The scoring scores other nodes relative to the one the client is connected to / the
	 * request is received on.
	 */

	/* Disqualify nodes which are above assoc limit */
	if ((!si->connected || si->node != candidate->node) &&
		candidate->node->n_assoc >= candidate->node->max_assoc && candidate->node->max_assoc)
		return 0;

	/* Disqualify nodes which are below min-signal */
	if (config.min_snr && candidate->signal < usteer_snr_to_signal(candidate->node, config.min_snr))
		return 0;

	information_age = current_time - candidate->information_timestamp;

	/* Age after seen_policy_timeout / 2 */
	age_penalty = -pow(4.0, (double)(information_age - (config.seen_policy_timeout * 0.5)) / (double)config.seen_policy_timeout) + 2.0;

	/* Factor betwwen 0.0 - 1.0 */
	age_penalty = MAX(0.0, MIN(1.0, age_penalty));
	
	return score * age_penalty;
}

void usteer_sta_generate_candidate_list(struct sta_info *si)
{
	struct sta *sta = si->sta;
	struct sta_info *si_seen;
	struct usteer_candidate *candidate;
	uint16_t tpt;

	/* Loop through seen STAs & estimate throughput */
	list_for_each_entry(si_seen, &sta->nodes, list) {
		/* In case STA is connected, filter for identical SSID */
		if (si->connected && strcmp(si_seen->node->ssid, si->node->ssid))
			continue;

		tpt = usteer_estimate_throughput(sta, si_seen->node, usteer_signal_to_snr(si_seen->node, si_seen->signal));
		candidate = usteer_candidate_get(sta, si_seen->node, true);

		candidate->information_source = CIS_STA_INFO;
		candidate->information_timestamp = si_seen->seen;

		candidate->signal = si_seen->signal;
		candidate->snr = usteer_signal_to_snr(si_seen->node, si_seen->signal);
		candidate->estimated_throughput = tpt;

		/* Build candidate score */
		candidate->score = usteer_score_candidate(si, candidate);
	}

	/* Sort candidate-list by score */
	usteer_candidate_list_sort(sta);
}
