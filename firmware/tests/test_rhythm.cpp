// Host tests for rhythm.h (docs/pins/keyring-oled/implementation-plan.md
// §3 «PR 4»): mapping, exact on/off edges, latest-wins, rate limit, the
// per-minute motor cap, quiet hours, millis rollover, idle-off.
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "../RailyPinsP1/rhythm.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

struct Span {
  uint32_t on;
  uint32_t off;
};

// Tick every millisecond in [from, from + count) and collect the motor-on
// spans as [on, off). Times wrap like millis().
static std::vector<Span> spans(RhythmPlayer& player, uint32_t from, uint32_t count) {
  std::vector<Span> out;
  bool was = false;
  uint32_t onAt = 0;
  for (uint32_t i = 0; i < count; i++) {
    const uint32_t now = from + i;
    const bool on = player.tick(now);
    if (on && !was) onAt = now;
    if (!on && was) out.push_back(Span{onAt, now});
    was = on;
  }
  if (was) out.push_back(Span{onAt, from + count});
  return out;
}

struct Event {
  uint32_t at;
  RhythmSignal signal;
};

// Like spans(), with signals triggered at their times inside the timeline.
static std::vector<Span> timeline(RhythmPlayer& player, uint32_t from, uint32_t count,
                                  const Event* events, size_t n) {
  std::vector<Span> out;
  bool was = false;
  uint32_t onAt = 0;
  for (uint32_t i = 0; i < count; i++) {
    const uint32_t now = from + i;
    for (size_t e = 0; e < n; e++) {
      if (events[e].at == now) player.trigger(events[e].signal, now);
    }
    const bool on = player.tick(now);
    if (on && !was) onAt = now;
    if (!on && was) out.push_back(Span{onAt, now});
    was = on;
  }
  if (was) out.push_back(Span{onAt, from + count});
  return out;
}

static bool sameSpans(const std::vector<Span>& got, const Span* want, size_t n) {
  if (got.size() != n) {
    printf("  got %zu spans:", got.size());
    for (size_t i = 0; i < got.size(); i++) printf(" [%u,%u)", got[i].on, got[i].off);
    printf("\n");
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    if (got[i].on != want[i].on || got[i].off != want[i].off) {
      printf("  span %zu: got [%u,%u) want [%u,%u)\n", i, got[i].on, got[i].off, want[i].on,
             want[i].off);
      return false;
    }
  }
  return true;
}

static void testMapping() {
  expect(rhythmForEventAck(0) == RHYTHM_NONE, "ack 0 is silent");
  expect(rhythmForEventAck(1) == RHYTHM_SENT, "ack 1 signal sent is ▮");
  expect(rhythmForEventAck(2) == RHYTHM_NONE, "ack 2 phone received is silent");
  expect(rhythmForEventAck(3) == RHYTHM_RESULT, "ack 3 result ready is ▬");
  expect(rhythmForEventAck(4) == RHYTHM_ERROR, "ack 4 didn't work is ▬▬▬");
  expect(rhythmForEventAck(5) == RHYTHM_NONE, "ack 5 is silent");
  expect(rhythmForEventAck(0x0F) == RHYTHM_NONE, "ack 0x0F is silent");
  expect(rhythmForEventAck(0x10) == RHYTHM_NONE, "0x10 heard (gesture) is silent here");
  expect(rhythmForEventAck(0x11) == RHYTHM_FOUND, "0x11 found for you is ▬ ▬");
  bool restSilent = true;
  for (unsigned ack = 0x12; ack <= 0x18; ack++) restSilent = restSilent && rhythmForEventAck((uint8_t)ack) == RHYTHM_NONE;
  expect(restSilent, "0x12-0x18 (done, error, counts, question) are silent here");
  expect(rhythmForEventAck(0x19) == RHYTHM_NONE, "0x19 is silent");
  expect(rhythmForEventAck(0xFF) == RHYTHM_NONE, "0xFF is silent");
  bool noneHasPattern = rhythmPattern(RHYTHM_NONE) == 0;
  expect(noneHasPattern, "RHYTHM_NONE has no pattern");
}

static void testPatternEdges() {
  expect(RHYTHM_TICK_MS == 80 && RHYTHM_BUZZ_MS == 300 && RHYTHM_GAP_MS == 150
             && RHYTHM_SPLIT_GAP_MS == 450,
         "timing constants are the owner's starting values");
  expect(rhythmOnTimeMs(RHYTHM_PATTERN_SENT) == 80, "▮ on-time 80 ms");
  expect(rhythmOnTimeMs(RHYTHM_PATTERN_RESULT) == 300, "▬ on-time 300 ms");
  expect(rhythmOnTimeMs(RHYTHM_PATTERN_ERROR) == 900, "▬▬▬ on-time 900 ms");
  expect(rhythmOnTimeMs(RHYTHM_PATTERN_FOUND) == 600, "▬ ▬ on-time 600 ms");

  {
    RhythmPlayer p = {};
    p.trigger(RHYTHM_SENT, 1000);
    const Span want[] = {{1000, 1080}};
    expect(sameSpans(spans(p, 1000, 3000), want, 1), "▮ is on [0, 80) ms");
  }
  {
    RhythmPlayer p = {};
    p.trigger(RHYTHM_RESULT, 1000);
    const Span want[] = {{1000, 1300}};
    expect(sameSpans(spans(p, 1000, 3000), want, 1), "▬ is on [0, 300) ms");
  }
  {
    RhythmPlayer p = {};
    p.trigger(RHYTHM_ERROR, 1000);
    const Span want[] = {{1000, 1300}, {1450, 1750}, {1900, 2200}};
    expect(sameSpans(spans(p, 1000, 3000), want, 3), "▬▬▬ is 300 on, 150 off, x3");
  }
  {
    RhythmPlayer p = {};
    p.trigger(RHYTHM_FOUND, 1000);
    const Span want[] = {{1000, 1300}, {1750, 2050}};
    expect(sameSpans(spans(p, 1000, 3000), want, 2), "▬ ▬ is 300 on, 450 off, 300 on");
  }
  {
    // Single-point edges, independent of the span helper.
    RhythmPlayer p = {};
    p.trigger(RHYTHM_SENT, 0);
    expect(p.tick(0), "▮ on at 0");
    expect(p.tick(79), "▮ still on at 79");
    expect(!p.tick(80), "▮ off at exactly 80");
  }
}

static void testIdleOff() {
  RhythmPlayer p = {};
  bool anyOn = false;
  for (uint32_t now = 0; now < 5000; now++) anyOn = anyOn || p.tick(now);
  expect(!anyOn, "fresh player keeps the motor off");

  p.trigger(RHYTHM_RESULT, 5000);
  expect(p.tick(5000), "a pattern turns the motor on");
  // loop() stalled for 10 s: the first tick after it must not report on.
  expect(!p.tick(15000), "no stuck-on motor after a long tick gap");
  anyOn = false;
  for (uint32_t now = 15000; now < 20000; now++) anyOn = anyOn || p.tick(now);
  expect(!anyOn, "motor stays off when idle after a pattern");

  RhythmPlayer q = {};
  q.trigger(RHYTHM_NONE, 0);
  expect(!q.tick(0) && !q.active, "a silent signal never starts the motor");
}

static void testLatestWins() {
  {
    // ▬▬▬ at 0, ▬ at 500: waits for the 800 ms start window, cuts the
    // error in its gap and keeps one gap of silence before the buzz.
    RhythmPlayer p = {};
    p.trigger(RHYTHM_ERROR, 0);
    for (uint32_t now = 1; now < 500; now++) p.tick(now);
    p.trigger(RHYTHM_RESULT, 500);
    const Span want[] = {{500, 750}, {900, 1200}};
    expect(sameSpans(spans(p, 500, 3000), want, 2),
           "newer ▬ replaces running ▬▬▬ at the start window with a clean gap");
  }
  {
    // ▬ ▬ at 0, ▮ at 850 lands mid second buzz: that buzz is cut at once and
    // the ▮ follows after a full gap, so the two never merge.
    RhythmPlayer p = {};
    p.trigger(RHYTHM_FOUND, 0);
    for (uint32_t now = 1; now < 850; now++) p.tick(now);
    p.trigger(RHYTHM_SENT, 850);
    expect(!p.tick(850), "replacement cuts a running buzz at once");
    const Span want[] = {{1000, 1080}};
    expect(sameSpans(spans(p, 851, 3000), want, 1), "replacement ▮ starts one gap after the cut");
  }
  {
    // Pending signals are one slot: the newest one wins, older ones vanish.
    RhythmPlayer p = {};
    const Event events[] = {{0, RHYTHM_SENT}, {100, RHYTHM_RESULT}, {200, RHYTHM_ERROR}};
    const Span want[] = {{0, 80}, {800, 1100}, {1250, 1550}, {1700, 2000}};
    expect(sameSpans(timeline(p, 0, 4000, events, 3), want, 4),
           "only the latest pending signal plays");
  }
  {
    RhythmPlayer p = {};
    const Event events[] = {{0, RHYTHM_SENT}, {10, rhythmForEventAck(2)}};
    const Span want[] = {{0, 80}};
    expect(sameSpans(timeline(p, 0, 2000, events, 2), want, 1),
           "a silent ack 2 does not cancel ▮");
  }
  {
    RhythmPlayer p = {};
    const Event events[] = {{0, RHYTHM_SENT}, {100, RHYTHM_RESULT}, {200, rhythmForEventAck(2)}};
    const Span want[] = {{0, 80}, {800, 1100}};
    expect(sameSpans(timeline(p, 0, 3000, events, 3), want, 2),
           "a silent ack 2 does not cancel a pending ▬");
  }
  {
    // Press flow: ack 1 then ack 3 shortly after both play, ▬ at 800.
    RhythmPlayer p = {};
    const Event events[] = {{0, rhythmForEventAck(1)}, {120, rhythmForEventAck(3)}};
    const Span want[] = {{0, 80}, {800, 1100}};
    expect(sameSpans(timeline(p, 0, 3000, events, 2), want, 2), "press flow ▮ then ▬ both play");
  }
}

static void testRateLimit() {
  {
    RhythmPlayer p = {};
    p.trigger(RHYTHM_SENT, 0);
    for (uint32_t now = 1; now < 799; now++) p.tick(now);
    p.trigger(RHYTHM_SENT, 799);
    expect(!p.tick(799), "second start is refused at 799 ms");
    expect(p.tick(800), "second start plays at exactly 800 ms");
  }
  {
    // A write every millisecond for 20 s: starts stay >= 800 ms apart and
    // no single on-span is longer than one ▮.
    RhythmPlayer p = {};
    std::vector<Span> got;
    bool was = false;
    uint32_t onAt = 0;
    for (uint32_t now = 0; now < 20000; now++) {
      p.trigger(RHYTHM_SENT, now);
      const bool on = p.tick(now);
      if (on && !was) onAt = now;
      if (!on && was) got.push_back(Span{onAt, now});
      was = on;
    }
    bool spaced = got.size() > 1;
    bool short_ = true;
    for (size_t i = 0; i < got.size(); i++) {
      if (i > 0 && got[i].on - got[i - 1].on < RHYTHM_MIN_START_INTERVAL_MS) spaced = false;
      if (got[i].off - got[i].on != RHYTHM_TICK_MS) short_ = false;
    }
    expect(spaced, "write flood starts at most one pattern per 800 ms");
    expect(short_, "write flood cannot stretch a ▮");
  }
}

// On-time of the motor in every 60 s window over a 10 min flood.
static uint32_t maxMinuteOnTime(const std::vector<bool>& level) {
  uint32_t window = 0;
  uint32_t best = 0;
  for (size_t i = 0; i < level.size(); i++) {
    if (level[i]) window++;
    if (i >= 60000 && level[i - 60000]) window--;
    if (window > best) best = window;
  }
  return best;
}

static void testMinuteCap() {
  const uint32_t duration = 600000;
  {
    // A stranger writes 4 every 10 ms: the longest pattern, as fast as the
    // radio allows.
    RhythmPlayer p = {};
    std::vector<bool> level(duration);
    uint32_t total = 0;
    uint32_t longest = 0;
    uint32_t run = 0;
    for (uint32_t now = 0; now < duration; now++) {
      if (now % 10 == 0) p.trigger(rhythmForEventAck(4), now);
      level[now] = p.tick(now);
      if (level[now]) {
        total++;
        run++;
        if (run > longest) longest = run;
      } else {
        run = 0;
      }
    }
    const uint32_t minute = maxMinuteOnTime(level);
    printf("  ▬▬▬ flood: max %u ms on in any 60 s, %u ms in 10 min\n", minute, total);
    expect(minute <= 3000, "▬▬▬ flood: motor on at most 3 s in any minute");
    expect(total <= RHYTHM_BUDGET_CAP_MS + duration / RHYTHM_REFILL_EVERY_MS,
           "▬▬▬ flood: 10 min on-time within the bucket bound");
    expect(total > 0, "▬▬▬ flood: the motor still signals within its budget");
    expect(longest <= RHYTHM_BUZZ_MS, "▬▬▬ flood: no single buzz longer than ▬");
  }
  {
    // Mixed flood cycling every signal.
    RhythmPlayer p = {};
    std::vector<bool> level(duration);
    const RhythmSignal cycle[] = {RHYTHM_FOUND, RHYTHM_ERROR, RHYTHM_RESULT, RHYTHM_SENT};
    for (uint32_t now = 0; now < duration; now++) {
      if (now % 7 == 0) p.trigger(cycle[(now / 7) % 4], now);
      level[now] = p.tick(now);
    }
    const uint32_t minute = maxMinuteOnTime(level);
    printf("  mixed flood: max %u ms on in any 60 s\n", minute);
    expect(minute <= 3000, "mixed flood: motor on at most 3 s in any minute");
  }
  {
    // Over budget, a signal is dropped (not cut short, not replayed late);
    // after a quiet minute the full budget is back.
    RhythmPlayer p = {};
    uint32_t now = 0;
    p.trigger(RHYTHM_ERROR, now);  // 900 of 1400
    for (; now < 1300; now++) p.tick(now);
    p.trigger(RHYTHM_FOUND, now);  // 600 > 500 + 32 earned: dropped
    bool anyOn = false;
    for (; now < 8000; now++) anyOn = anyOn || p.tick(now);
    expect(!anyOn, "an over-budget signal is dropped, not played late or cut");
    p.trigger(RHYTHM_RESULT, now);  // ▬ fits the remaining budget
    expect(p.tick(now), "a signal that fits the remaining budget plays");
    now += 60000;
    p.tick(now);
    expect(p.budgetMs == RHYTHM_BUDGET_CAP_MS, "budget refills to the cap after a quiet minute");
    p.trigger(RHYTHM_ERROR, now);
    const Span want[] = {{now, now + 300}, {now + 450, now + 750}, {now + 900, now + 1200}};
    expect(sameSpans(spans(p, now, 3000), want, 3), "full ▬▬▬ plays again after the refill");
  }
}

static void testQuietHours() {
  {
    RhythmPlayer p = {};
    p.setQuietHours(true, 0);
    p.trigger(RHYTHM_FOUND, 10);
    bool anyOn = false;
    for (uint32_t now = 10; now < 3000; now++) anyOn = anyOn || p.tick(now);
    expect(!anyOn, "quiet hours: found for you stays silent");
  }
  {
    RhythmPlayer p = {};
    p.trigger(RHYTHM_ERROR, 0);
    p.trigger(RHYTHM_RESULT, 100);  // pending
    for (uint32_t now = 1; now < 200; now++) p.tick(now);
    p.setQuietHours(true, 200);
    expect(!p.tick(200), "quiet hours stop a running buzz at once");
    bool anyOn = false;
    for (uint32_t now = 200; now < 5000; now++) anyOn = anyOn || p.tick(now);
    expect(!anyOn, "quiet hours drop the rest of the pattern and the pending one");
    p.setQuietHours(false, 5000);
    anyOn = false;
    for (uint32_t now = 5000; now < 8000; now++) anyOn = anyOn || p.tick(now);
    expect(!anyOn, "leaving quiet hours does not replay a dropped signal");
    p.trigger(RHYTHM_SENT, 8000);
    const Span want[] = {{8000, 8080}};
    expect(sameSpans(spans(p, 8000, 2000), want, 1), "after quiet hours signals play again");
  }
  {
    RhythmPlayer p = {};
    p.trigger(RHYTHM_RESULT, 0);
    p.cancel(100);
    expect(!p.tick(100) && !p.active, "cancel turns the motor off");
  }
}

static void testRollover() {
  {
    RhythmPlayer p = {};
    const uint32_t start = UINT32_MAX - 499;  // 500 ms before the wrap
    p.trigger(RHYTHM_ERROR, start);
    const Span want[] = {{start, start + 300}, {start + 450, start + 750},
                         {start + 900, start + 1200}};
    expect(sameSpans(spans(p, start, 3000), want, 3), "▬▬▬ edges are exact across the wrap");
    expect(start + 750 == 250u, "second buzz of the wrap test ends after 0");
  }
  {
    RhythmPlayer p = {};
    const uint32_t start = UINT32_MAX - 99;
    p.trigger(RHYTHM_SENT, start);
    for (uint32_t i = 1; i < 700; i++) p.tick(start + i);
    p.trigger(RHYTHM_SENT, start + 700);  // 600 after the wrap
    expect(!p.tick(start + 700), "rate limit holds across the wrap");
    for (uint32_t i = 701; i < 800; i++) p.tick(start + i);
    expect(p.tick(start + 800), "next start plays 800 ms later across the wrap");
  }
  {
    // The start window itself straddles the wrap.
    RhythmPlayer p = {};
    const uint32_t start = UINT32_MAX - 399;
    p.trigger(RHYTHM_SENT, start);
    for (uint32_t i = 1; i < 300; i++) p.tick(start + i);
    p.trigger(RHYTHM_RESULT, start + 300);  // still before the wrap
    expect(!p.tick(start + 300), "rate limit holds when its window crosses the wrap");
    for (uint32_t i = 301; i < 800; i++) p.tick(start + i);
    expect(p.tick(start + 800), "queued ▬ plays when that window ends after the wrap");
  }
  {
    // Budget refill measures time with unsigned subtraction too.
    RhythmPlayer p = {};
    const uint32_t start = UINT32_MAX - 999;
    p.trigger(RHYTHM_ERROR, start);
    for (uint32_t i = 1; i <= 2000; i++) p.tick(start + i);
    const uint16_t before = p.budgetMs;
    p.tick(start + 2000 + 4000);  // 4 s later, after the wrap
    expect(p.budgetMs == before + 100, "budget refills 1 ms per 40 ms across the wrap");
  }
}

// The event_ack write → LED byte and raw motor byte (onAckWrite, serial c).
static void testRawAckPath() {
  const uint8_t zero = 0, found = 0x11, sent = 1, big = 0x42;
  EventAckBytes b = eventAckBytes(&zero, 1);
  expect(b.led == 1 && rhythmForEventAck(b.rhythm) == RHYTHM_NONE, "a zero write flashes once and never vibrates");
  b = eventAckBytes(&zero, 0);
  expect(b.led == 1 && rhythmForEventAck(b.rhythm) == RHYTHM_NONE, "an empty write flashes once and never vibrates");
  b = eventAckBytes(0, 1);
  expect(b.led == 1 && b.rhythm == 0, "no buffer is an empty write");
  b = eventAckBytes(&found, 1);
  expect(b.led == 0x11 && rhythmForEventAck(b.rhythm) == RHYTHM_FOUND,
         "0x11 keeps today's LED byte and plays «found for you» from the raw byte");
  b = eventAckBytes(&sent, 1);
  expect(b.led == 1 && rhythmForEventAck(b.rhythm) == RHYTHM_SENT, "ack 1: one flash, ▮");
  b = eventAckBytes(&big, 1);
  expect(b.led == 0x42 && rhythmForEventAck(b.rhythm) == RHYTHM_NONE, "an unknown byte is silent");
}

int main() {
  testRawAckPath();
  testMapping();
  testPatternEdges();
  testIdleOff();
  testLatestWins();
  testRateLimit();
  testMinuteCap();
  testQuietHours();
  testRollover();
  return failures ? 1 : 0;
}
