/*
 * \brief   Schedules CPU shares for the execution time of a CPU
 * \author  Martin Stein
 * \date    2014-10-09
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__KERNEL__CPU_SCHEDULER_H_
#define _CORE__INCLUDE__KERNEL__CPU_SCHEDULER_H_

/* core includes */
#include <util.h>
#include <kernel/configuration.h>
#include <kernel/double_list.h>
#include <kernel/cpu_scheduler_util.h>
#include <kernel/quota_scheduler.h>

namespace Kernel
{
	class Cpu_scheduler;
}

class Kernel::Cpu_scheduler : public Kernel::Quota_scheduler
{
	private:
		typedef Cpu_share                Share;
		typedef Cpu_fill                 Fill;
		typedef Cpu_claim                Claim;
		typedef Double_list_typed<Claim> Claim_list;
		typedef Double_list_typed<Fill>  Fill_list;
		typedef Cpu_priority             Prio;


	public:
		/**
		 * Constructor
		 *
		 * \param i  Gets scheduled with static quota when no other share
		 *           is schedulable. Unremovable. All values get ignored.
		 * \param q  total amount of time quota that can be claimed by shares
		 * \param f  time-slice length of the fill round-robin
		 */
		Cpu_scheduler(Share * const i, unsigned const q, unsigned const f) : Quota_scheduler(i, q, f) { };

};

#endif /* _CORE__INCLUDE__KERNEL__CPU_SCHEDULER_H_ */
