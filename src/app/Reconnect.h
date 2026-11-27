// Waiting between reconnect attempts (M6): retry schedule and the countdown prompt.
#pragma once

#include <string>

namespace winssh::app
{

// Seconds to wait before retry number `attempt` (1-based): 1, 2, 4, 8, then 10 s each time.
int RetryDelaySeconds(int attempt);

enum class WaitChoice
{
    RetryNow, // the delay ran out, or the user pressed Enter / r
    Stop,     // Esc, q, Ctrl+C, or Ctrl+] x
};

// Shows a one-line countdown, updated every second:
//   [winssh] reconnecting to root@10.0.0.108 in 4 s (attempt 3) - Enter: now, Esc: stop
// The console must be in raw input mode. Keys pressed here are consumed.
WaitChoice WaitBeforeRetry(const std::string &target, int attempt, int seconds);

// Puts the terminal back to normal after a session ends unexpectedly: resets colors, shows the
// cursor, turns off mouse reporting, bracketed paste and app keypad modes. `leaveAltScreen` adds
// "leave the alternate screen"; only pass true when the session was in it (vim, less, htop),
// because that sequence also restores the cursor position saved when the alternate screen was
// entered, which moves the cursor to the top of the screen when it never was.
std::string TerminalResetSequence(bool leaveAltScreen);

} // namespace winssh::app
