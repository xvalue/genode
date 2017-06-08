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
    _residual = _quota;
    //_for_each_prio([&] (unsigned const p) { _reset_claims(p); });
}


template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_consumed(unsigned const q)
{
    if (_residual > q) { _residual -= q; }
    else { _next_round(); }
    Replenishment *rep = _rts.head();
    if (!rep) {return;} /* No replenish timings, no updating  needed */

    if (rep->_replenish_time <= q) {
        Share * s = rep->_share;
        if (!s->_claim) {
            _rcl[s->_prio].insert_tail(s);
        }
        //Genode::log("Replenish q = ", rep->_consumed_quota);
        s->_claim += rep->_consumed_quota;
        rep->_share = nullptr;
        _rts.remove(rep);
    }
    else {
        rep->_replenish_time -= q;
    }
    if (_total_replenish >= q) {
        _total_replenish -= q;
    }

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
void Cpu_scheduler<Q, M>::_head_claimed(unsigned const r, unsigned q)
{
	if (!_head->_quota) { return; }
	_head->_claim = r > _head->_quota ? _head->_quota : r;
    //Genode::log("consumed q = ", q, " Residual quota r = ", r);
    _add_replenishment(_head, q);
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
    for (signed p = Prio::MAX; p > Prio::MIN - 1; p--) {
        Share * const s = _share(_rcl[p].head());
        if (!s) { continue; }
        if (!s->_claim) { continue; }
        _set_head(s, s->_claim, 1);
        return 1;
    }
    return 0;
}

template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_add_replenishment(Share * const s, unsigned const q) {
    Replenishment *rep = _new_replenishment();
    assert(rep != nullptr);

    rep->_consumed_quota = q;
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
}


template<unsigned Q, unsigned M>
bool Cpu_scheduler<Q, M>::_fill_for_head()
{
    Share * const s = _share(_fills.head());
    if (!s) { return 0; }

    _set_head(s, s->_fill, 0);
	return 1;
}


template<unsigned Q, unsigned M>
unsigned Cpu_scheduler<Q, M>::_trim_consumption(unsigned & q)
{
    //q = Genode::min(Genode::min(q, _head_quota), _residual);
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
	if (_head) {
		if (_head_claims) { _head_claimed(r, q); }
		else              { _head_filled(r);  }
		_consumed(q);
	}


	if (_claim_for_head(r)) { return; }
	if (_fill_for_head()) { return; }


	_set_head(_idle, _fill, 0);

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
	if (!s->_quota) { return; }
	if (s->_ready) { _rcl[s->_prio].remove(s); }
	else { _ucl[s->_prio].remove(s); }

    Replenishment *rep = _rts.head();
    if (!rep) { return; }
    
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
    
    
}


template<unsigned Q, unsigned M>
Cpu_replenishment * Cpu_scheduler<Q, M>::_new_replenishment() {
    //Replenishment *rep = _replenish_cache.head();
    //_replenish_cache.remove(rep);
    //return rep;
    Replenishment *rep = &(_replenishments[_rep_counter]);
    _rep_counter = (_rep_counter + 1) % 10000;
    return rep;
};

template<unsigned Q, unsigned M>
void Cpu_scheduler<Q, M>::_reset_replenishment(Replenishment * rep) {
    _rts.remove(rep);
    rep->_share = nullptr;
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
void Cpu_scheduler<Q, M>::reset_claims() 
{
    Replenishment * rep = _rts.head();
    while(!rep) {
        Genode::log("alter");
        Replenishment * tmp = Replenishment_list::next(rep);
        _rts.remove(rep);
        rep = tmp;
    }
    _for_each_prio([&] (unsigned const p) { _reset_claims(p); });
}


template<unsigned Q, unsigned M>
Cpu_scheduler<Q, M>::Cpu_scheduler(Share * const i, unsigned const q, unsigned const f)
: _idle(i), _head_yields(0), _quota(q), _fill(f),
    _residual(q), _total_replenish(0), _rep_counter(0)
{ 
    _set_head(i, f, 0); 
    
    for (auto i : _replenishments) {
        _replenish_cache.insert_tail(&i);
    }

}
