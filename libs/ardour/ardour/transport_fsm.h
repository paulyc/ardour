#ifndef _ardour_transport_fsm_h_
#define _ardour_transport_fsm_h_

#include <boost/weak_ptr.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>

#include "pbd/stacktrace.h"

/* state machine */
namespace msm = boost::msm;
namespace mpl = boost::mpl;

namespace ARDOUR
{

class TransportAPI;

struct TransportFSM : public msm::front::state_machine_def<TransportFSM>
{
	/* events to be delivered to the FSM */

	struct butler_done {};

	struct butler_required {};
	struct declick_done {};
	struct start_transport {};

	struct stop_transport {
		stop_transport (bool ab = false, bool cl = false)
			: abort (ab)
			, clear_state (cl) {}

		bool abort;
		bool clear_state;
	};

	struct locate {
		locate ()
			: target (0)
			, with_roll (false)
			, with_flush (false)
			, with_loop (false)
			, force (false) {}

		locate (samplepos_t target, bool roll, bool flush, bool loop, bool f4c)
			: target (target)
			, with_roll (roll)
			, with_flush (flush)
			, with_loop (loop)
			, force (f4c) {}

		samplepos_t target;
		bool with_roll;
		bool with_flush;
		bool with_loop;
		bool force;
	};

	struct locate_done {};

	/* Flags */

	struct DeclickOutInProgress {};
	struct LocateInProgress {};

	typedef msm::active_state_switch_before_transition active_state_switch_policy;

	/* transition actions */

	void start_playback (start_transport const& p);
	void roll_after_locate (locate_done const& p);
	void stop_playback (stop_transport const& s);
	void start_locate (locate const& s);
	void start_saved_locate (declick_done const& s);
	void interrupt_locate (locate const& s);
	void schedule_butler_for_transport_work (butler_required const&);
	void save_locate_and_stop (locate const &);

	/* guards */

	bool should_roll_after_locate (locate_done const &);
	bool should_not_roll_after_locate (locate_done const & e)  { return !should_roll_after_locate (e); }

#define define_state(State) \
	struct State : public msm::front::state<> \
	{ \
		template <class Event,class FSM> void on_entry (Event const&, FSM&) { std::cout << "entering: " # State << std::endl; } \
		template <class Event,class FSM> void on_exit (Event const&, FSM&) {std::cout << "leaving: " # State << std::endl;} \
	}

#define define_state_flag(State,Flag) \
	struct State : public msm::front::state<> \
	{ \
		template <class Event,class FSM> void on_entry (Event const&, FSM&) { std::cout << "entering: " # State << std::endl; } \
		template <class Event,class FSM> void on_exit (Event const&, FSM&) {std::cout << "leaving: " # State << std::endl;} \
		typedef mpl::vector1<Flag> flag_list; \
	}

#define define_state_flag2(State,Flag1,Flag2) \
	struct State : public msm::front::state<> \
	{ \
		template <class Event,class FSM> void on_entry (Event const&, FSM&) { std::cout << "entering: " # State << std::endl; } \
		template <class Event,class FSM> void on_exit (Event const&, FSM&) {std::cout << "leaving: " # State << std::endl;} \
		typedef mpl::vector2<Flag1,Flag2> flag_list; \
	}

	/* FSM states */

	define_state (WaitingForButler);
	define_state (NotWaitingForButler);
	define_state (Stopped);
	define_state (Rolling);
	define_state (MasterWait);
	define_state_flag2(DeclickToLocate,LocateInProgress,DeclickOutInProgress);
	define_state_flag(WaitingToEndLocate,LocateInProgress);
	define_state_flag(DeclickOut,DeclickOutInProgress);

	// Pick a back-end
	typedef msm::back::state_machine<TransportFSM> back;

	boost::weak_ptr<back> wp;

	bool locating (declick_done const &) { return backend()->is_flag_active<LocateInProgress>(); }

	static boost::shared_ptr<back> create(TransportAPI& api) {

		boost::shared_ptr<back> p (new back ());

		p->wp = p;
		p->api = &api;
		return p;
	}

	boost::shared_ptr<back> backend() { return wp.lock(); }

	template<typename Event> void enqueue (Event const & e) {
		backend()->process_event (e);
	}

	/* the initial state */
	typedef boost::mpl::vector<Stopped,NotWaitingForButler> initial_state;

	/* transition table */
	typedef TransportFSM T; // makes transition table cleaner

	struct transition_table : mpl::vector<
		//      Start     Event         Next      Action               Guard
		//    +----------+-------------+----------+---------------------+----------------------+
		a_row < Stopped  , start_transport       , Rolling    , &T::start_playback                     >,
		_row  < Stopped  , stop_transport        , Stopped                                              >,
		a_row < Stopped  , butler_required       , WaitingForButler , &T::schedule_butler_for_transport_work >,
		_row  < Stopped  , butler_done           , Stopped    >,

		//    +----------+-------------+----------+---------------------+----------------------+
		a_row  < Stopped  , locate                , WaitingToEndLocate, &T::start_locate>,  /* will send butler_required, then locate_done */
		a_row  < Rolling  , locate                , DeclickToLocate, &T::save_locate_and_stop>, /* will send declick_done, butler_required, then locate_done */
		//    +----------+-------------+----------+---------------------+----------------------+
		_row  < Rolling  , start_transport       , Rolling                                    >,
		a_row < Rolling  , stop_transport        , DeclickOut , &T::stop_playback             >,
		_row  < Rolling  , butler_done           , Rolling                                    >,
		//    +----------+-------------+----------+---------------------+----------------------+
		a_row  < DeclickToLocate,  declick_done, WaitingToEndLocate, &T::start_saved_locate >,

		_row < DeclickOut, declick_done, Stopped >,
		_row < WaitingForButler, butler_done , Stopped >,

		a_row < NotWaitingForButler, butler_required , WaitingForButler , &T::schedule_butler_for_transport_work >,
		a_row < WaitingForButler, butler_required , WaitingForButler , &T::schedule_butler_for_transport_work >,
		_row  < WaitingForButler, butler_done  , NotWaitingForButler >,

		row  < WaitingToEndLocate, locate_done, Rolling, &T::roll_after_locate, &T::should_roll_after_locate >,
		g_row < WaitingToEndLocate, locate_done, Stopped, &T::should_not_roll_after_locate >,

		/* if a new locate request arrives while we're in the
		 * middling of dealing with this one, make it interrupt
		 * the current locate with a new target.
		 */

		a_row < WaitingToEndLocate, locate, WaitingToEndLocate, &T::interrupt_locate >,
		a_row < DeclickToLocate, locate, DeclickToLocate, &T::interrupt_locate >

		// Deferrals

#define defer(start_state,ev) boost::msm::front::Row<start_state, ev, boost::msm::front::none , boost::msm::front::Defer, boost::msm::front::none >

		//defer (Locating, start_transport),
		//defer (Locating, stop_transport),
		//defer (ButlerWait, start_transport),
		//defer (ButlerWait, stop_transport)
		//    +----------+-------------+----------+---------------------+----------------------+
		> {};

	// typedef int activate_deferred_events;

	locate _last_locate;

	TransportAPI* api;

	// Replaces the default no-transition response.
	template <class FSM,class Event>
	void no_transition(Event const& e, FSM&,int state)
	{
		std::cout << "FSM: no transition from state " << state
		          << " on event " << typeid(e).name() << std::endl;
	}
};

} /* end namespace ARDOUR */

#endif
