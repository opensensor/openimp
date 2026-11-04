/* Teardown stall watch (issue #198): a thread that only reports.
 *
 * at_td_arm() starts it before the teardown, at_td_step() names the call the main thread is about to make,
 * at_td_disarm() stops it.  If the teardown is not finished after APITEST_TEARDOWN_WATCH_MS (default 8000)
 * the thread writes ONE block (steps, FrameSource state word / g_fs_lock holder and owner, every thread of
 * the process with comm/state/wchan/syscall/stack) to stderr and to a file next to the results, then
 * goes on waiting; it never kills or touches anything (the 60 s alarm watchdog stays the hard timeout).
 * APITEST_NO_TEARDOWN_WATCH=1 switches it off.  A teardown that finishes in time writes nothing. */
#ifndef AT_TDWATCH_H
#define AT_TDWATCH_H

void at_td_init(const char *json_path);     /* where the report file goes: the directory of the --json file, else /tmp */
void at_td_arm(const char *label);
void at_td_step(const char *step);          /* a string literal: the call about to be made */
void at_td_disarm(void);

#endif
