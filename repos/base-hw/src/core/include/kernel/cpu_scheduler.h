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

#include <base/log.h>
namespace Kernel
{
	/**
	 * Priority of an unconsumed CPU claim versus other unconsumed CPU claims
	 */
	class Cpu_priority;

	/**
	 * Scheduling context that has quota and priority (low-latency)
	 */
	class Cpu_claim : public Double_list_item { };

	/**
	 * Scheduling context that has no quota or priority (best effort)
	 */
	class Cpu_fill  : public Double_list_item { };

	/**
	 * Scheduling context that is both claim and fill
	 */
	class Cpu_share;

	/**
	 * Schedules CPU shares for the execution time of a CPU
	 */
    template<unsigned Q, unsigned M>
	class Cpu_scheduler;

	/**
	 * Scheduling context that saves start-execution time and quota consumend
	 */
    class Cpu_replenishment;

}

class Kernel::Cpu_priority
{
	private:

		unsigned _value;

	public:

		enum {
			MIN = 0,
			MAX = cpu_priorities - 1,
		};

		/**
		 * Construct priority with value 'v'
		 */
		Cpu_priority(signed const v) : _value(Genode::min(v, MAX)) { }

		/*
		 * Standard operators
		 */

		Cpu_priority & operator =(signed const v)
		{
			_value = Genode::min(v, MAX);
			return *this;
		}

		operator signed() const { return _value; }
};

class Kernel::Cpu_share : public Cpu_claim, public Cpu_fill
{
    template<unsigned Q, unsigned M>
	friend class Cpu_scheduler;

	private:

		signed const _prio;
		unsigned     _quota;
		unsigned     _claim;
		unsigned     _fill;
		bool         _ready;

	public:

		/**
		 * Constructor
		 *
		 * \param p  claimed priority
		 * \param q  claimed quota
		 */
		Cpu_share(signed const p, unsigned const q)
		: _prio(p), _quota(q), _claim(q), _ready(0) { }

		/*
		 * Accessors
		 */

		bool ready() const { return _ready; }
		void quota(unsigned const q) { _quota = q; }
};

class Kernel::Cpu_replenishment : public Double_list_item {
    public:
        unsigned _consumed_quota;
        unsigned long _replenish_time;
        Cpu_share *_share;
};

//template<typename T, typename U>
//class Kernel::Cpu_scheduler { }; [> this should raise an error <] 

template< unsigned Q, unsigned M> // TODO: check min quota
class Kernel::Cpu_scheduler
{
	private:
        enum {
            _quota = Q,
            _min_quota = M,
        };

		typedef Cpu_share                Share;
		typedef Cpu_fill                 Fill;
		typedef Cpu_claim                Claim;
		typedef Double_list_typed<Claim> Claim_list;
		typedef Double_list_typed<Fill>  Fill_list;
		typedef Cpu_priority             Prio;
        typedef Cpu_replenishment        Replenishment;
        typedef Double_list_typed<Replenishment> Replenishment_list;

		Claim_list     _rcl[Prio::MAX + 1]; /* ready claims */
		Claim_list     _ucl[Prio::MAX + 1]; /* unready claims */
		Fill_list      _fills;              /* ready fills */
        Replenishment_list _rts;            /* replenishment timings */
		Share * const  _idle;
		Share *        _head;
		unsigned       _head_quota;
		bool           _head_claims;
		bool           _head_yields;
		//unsigned const _quota;
		unsigned const _fill;

        unsigned _total_replenish;
        unsigned _current_consumption;

        //Replenishment _replenishments[ _quota / _min_quota];
        Replenishment _replenishments[1000];
        Replenishment_list _replenish_cache;

		template <typename F> void _for_each_prio(F f) {
			for (signed p = Prio::MAX; p > Prio::MIN - 1; p--) { f(p); } }

		template <typename T>
		static Share * _share(T * const t) { return static_cast<Share *>(t); }

		static void _reset(Claim * const c);

		void     _reset_claims(unsigned const p);
		void     _next_round();
		void     _consumed(unsigned const q);
		void     _set_head(Share * const s, unsigned const q, bool const c);
		void     _next_fill();
		void     _head_claimed(unsigned const r);
		void     _head_filled(unsigned const r);
		bool     _claim_for_head(unsigned q);
		bool     _fill_for_head();
		unsigned _trim_consumption(unsigned & q);
        Replenishment * _new_replenishment();
        void _reset_replenishment(Replenishment * rep);

		/**
		 * Fill 's' becomes a claim due to a quota donation
		 */
		void _quota_introduction(Share * const s);

		/**
		 * Claim 's' looses its state as claim due to quota revokation
		 */
		void _quota_revokation(Share * const s);

		/**
		 * The quota of claim 's' changes to 'q'
		 */
		void _quota_adaption(Share * const s, unsigned const q);

	public:

		/**
		 * Constructor
		 *
		 * \param i  Gets scheduled with static quota when no other share
		 *           is schedulable. Unremovable. All values get ignored.
		 * \param q  total amount of time quota that can be claimed by shares
		 * \param f  time-slice length of the fill round-robin
		 */
		Cpu_scheduler(Share * const i, unsigned const q, unsigned const f);

		/**
		 * Update head according to the consumption of quota 'q'
		 */
		void update(unsigned q);

		/**
		 * Set 's1' ready and return wether this outdates current head
		 */
		bool ready_check(Share * const s1);

		/**
		 * Set share 's' ready
		 */
		void ready(Share * const s);

		/**
		 * Set share 's' unready
		 */
		void unready(Share * const s);

		/**
		 * Current head looses its current claim/fill for this round
		 */
		void yield();

		/**
		 * Remove share 's' from scheduler
		 */
		void remove(Share * const s);

		/**
		 * Insert share 's' into scheduler
		 */
		void insert(Share * const s);

		/**
		 * Set quota of share 's' to 'q'
		 */
		void quota(Share * const s, unsigned const q);

		/*
		 * Accessors
		 */

		Share * head() const { 
            //Genode::log("head aufgerufen");
            return _head; }
		unsigned head_quota() const {
            unsigned q = _head_quota;
            Genode::log("###### head quota", q);
			return _head_quota; };
		unsigned quota() const { 
            unsigned q = _quota;
            Genode::log("@@@@ quota ", q);
            return _quota; }
		unsigned residual() const { 
            Genode::log("ich habe keine lust mehr");
            return 0; } // TODO: wofuer ist das eigentlich
};

//TODO: ich hab doch keine ahnung
template class Kernel::Cpu_scheduler<7816000, 10>;

#endif /* _CORE__INCLUDE__KERNEL__CPU_SCHEDULER_H_ */
