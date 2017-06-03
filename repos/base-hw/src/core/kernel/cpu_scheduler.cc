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

/* Genode includes */
#include <base/log.h>

/* core includes */
#include <kernel/cpu_scheduler.h>
#include <assert.h>

using namespace Kernel;


template <unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_reset(Claim * const c) {
	_share(c)->_claim = _share(c)->_quota; }


template <unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_reset_claims(unsigned const p)
{
	_rcl[p].for_each([&] (Claim * const c) { _reset(c); });
	_ucl[p].for_each([&] (Claim * const c) { _reset(c); });
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_next_round()
{
	//_residual = _quota;
	//_for_each_prio([&] (unsigned const p) { _reset_claims(p); });
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_consumed(unsigned const q)
{
    unsigned b = q;
    Genode::log("Consumed q = ", b );
    Replenishment *rep = _rts.head();
    _head_quota -= q;
    if (!rep) {return;} /* No replenish timings, no updating  needed */

    /*
    if (rep->_replenish_time <= q) {
        Genode::log("Replenished ", rep->_consumed_quota, " Quota");
        rep->_share->_claim += rep->_consumed_quota;
        _reset_replenishment(rep);

        if (_total_replenish >= q) {
            _total_replenish -= q;
        }
        return;
    }
    rep->_replenish_time =- q;
    */
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_set_head(Share * const s, unsigned const q, bool const c)
{
	_head_quota  = q;
	_head_claims = c;
	_head        = s;
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_next_fill()
{
	_head->_fill = _fill;
	_fills.head_to_tail();
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_head_claimed(unsigned const r)
{
	if (!_head->_quota) { return; }
	_head->_claim = r > _head->_quota ? _head->_quota : r;
	if (_head->_claim || !_head->_ready) { return; }
	_rcl[_head->_prio].to_tail(_head);
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_head_filled(unsigned const r)
{
	if (_fills.head() != _head) { return; }
	if (r) { _head->_fill = r; }
	else { _next_fill(); }
}


template<unsigned Q, unsigned M>
bool Cpu_scheduler<Q, M>::_claim_for_head(unsigned r)
{
    return 0;
    //Genode::log("_claim_for_head start");
	for (signed p = Prio::MAX; p > Prio::MIN - 1; p--) {
		Share * const s = _share(_rcl[p].head());
		if (!s) { continue; }
		if (!s->_claim) { continue; }

        _current_consumption += r;
        if (_head != s && _head_claims) { // neuer job, claim -> claim
            Replenishment *rep   = _new_replenishment();
            Genode::log("new replenishment");
            rep->_consumed_quota = _current_consumption;
            rep->_share          = s;

            Replenishment *rhead = _rts.head();
            if (!rhead) {
                _total_replenish     = _quota;
                rep->_replenish_time = _quota;
            } else {
                rep->_replenish_time  = _quota - _total_replenish;
                _total_replenish     += rep->_replenish_time;
            }
            _rts.insert_tail(rep);

            _current_consumption = 0;
        }
        else if (_head == s) { // job == alter job
            //Genode::log("current_consumption " , _current_consumption );
            return 1;
        }
        else { // idle/fill -> claim
            _current_consumption = 0;
        }

		_set_head(s, s->_claim, 1);
    Genode::log("_claim_for_head end1");
		return 1;
	}
    //Genode::log("_claim_for_head end2");
	return 0;
}


template<unsigned Q, unsigned M>
bool Cpu_scheduler<Q, M>::_fill_for_head()
{
    // hier stimmt irgendwas nicht
    /*
	 *Share * const s = _share(_fills.head());
	 *if (!s) { return 0; }
	 *_set_head(s, s->_fill, 0);
     */
	return 0;
}


template<unsigned Q, unsigned M>
unsigned Cpu_scheduler<Q, M>::_trim_consumption(unsigned & q)
{
	q = Genode::min(q, _head_quota);
	if (!_head_yields) { return _head_quota - q; }
	_head_yields = 0;
	return 0;
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_quota_introduction(Share * const s)
{
	if (s->_ready) { _rcl[s->_prio].insert_tail(s); }
	else { _ucl[s->_prio].insert_tail(s); }
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_quota_revokation(Share * const s)
{
	if (s->_ready) { _rcl[s->_prio].remove(s); }
	else { _ucl[s->_prio].remove(s); }
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_quota_adaption(Share * const s, unsigned const q)
{
	if (q) { if (s->_claim > q) { s->_claim = q; } }
	else { _quota_revokation(s); }
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::update(unsigned q)
{
	/* do not detract the quota if the head context was removed even now */
    unsigned const r = _trim_consumption(q);
    //Genode::log("update");
	if (_head) {
		if (_head_claims) { _head_claimed(r); }
		else              { _head_filled(r);  }
		_consumed(q);
	}

	if (_claim_for_head(r)) { return; }
	if (_fill_for_head()) { return; }
    Genode::log("Idle bro");
	_set_head(_idle, _fill, 0);

    //Genode::log("update end");
}


template<unsigned Q, unsigned M>
bool Cpu_scheduler<Q, M>::ready_check(Share * const s1)
{
	assert(_head);

	ready(s1);
	Share * s2 = _head;
	if (!s1->_claim) { return s2 == _idle; }
	if (!_head_claims) { return 1; }
	if (s1->_prio != s2->_prio) { return s1->_prio > s2->_prio; }
	for (; s2 && s2 != s1; s2 = _share(Claim_list::next(s2))) ;
	return !s2;
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::ready(Share * const s)
{
	assert(!s->_ready && s != _idle);
	s->_ready = 1;
	s->_fill = _fill;
	_fills.insert_tail(s);
	if (!s->_quota) { return; }
	_ucl[s->_prio].remove(s);
	if (s->_claim) { _rcl[s->_prio].insert_head(s); }
	else { _rcl[s->_prio].insert_tail(s); }
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::unready(Share * const s)
{
	assert(s->_ready && s != _idle);
	s->_ready = 0;
	_fills.remove(s);
	if (!s->_quota) { return; }
	_rcl[s->_prio].remove(s);
	_ucl[s->_prio].insert_tail(s);
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::yield() { _head_yields = 1; }


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::remove(Share * const s)
{
	assert(s != _idle);

	if (s == _head) _head = nullptr;
	if (s->_ready) { _fills.remove(s); }
	if (!s->_quota) { return; } // TODO: check if replenishment
	if (s->_ready) { _rcl[s->_prio].remove(s); }
	else { _ucl[s->_prio].remove(s); }

    Replenishment *rep = _rts.head();
    if (!rep) { return; }
    /*
    while (rep) {
        if (rep->_share == s) {
            Replenishment *tmp = Replenishment_list::next(rep);
            if (!tmp) {
                _reset_replenishment(rep);
                return;
            }
            tmp->_replenish_time += rep->_replenish_time;
            _reset_replenishment(rep);
            rep = tmp;
        }
    }
    */
}


template<unsigned Q, unsigned M>
Cpu_replenishment * Cpu_scheduler<Q, M>::_new_replenishment() {
    Replenishment *rep = _replenish_cache.head();
    _replenish_cache.remove(rep);
    return rep;
};

template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_reset_replenishment(Replenishment * rep) {
    _rts.remove(rep);
    rep->_share = nullptr;
    _replenish_cache.insert_tail(rep);
}

template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::insert(Share * const s)
{
	assert(!s->_ready);
	if (!s->_quota) { return; }
	s->_claim = s->_quota;
	_ucl[s->_prio].insert_head(s);
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::quota(Share * const s, unsigned const q)
{
	assert(s != _idle);
	if (s->_quota) { _quota_adaption(s, q); }
	else if (q) { _quota_introduction(s); }
	s->_quota = q;
}


template<unsigned Q, unsigned M>
Cpu_scheduler<Q, M>::Cpu_scheduler(Share * const i, unsigned const q, unsigned const f)
: _idle(i), _head_yields(0), _fill(f), _total_replenish(0), _current_consumption(0)
{ 
    _set_head(i, f, 0); 
    unsigned b = q;
    
    for (auto i : _replenishments) {
        _replenish_cache.insert_tail(&i);
    }
    

}
