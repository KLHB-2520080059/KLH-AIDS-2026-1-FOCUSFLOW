// Dynamic priority scheduler simulation (runs in the browser).
//
// Each tick:
//  1. I/O tasks that finished waiting come back with a priority boost.
//  2. The ready task with the highest score runs for one tick.
//  3. Every other ready task ages (its priority grows while it waits).
//  4. The runner gets a CPU penalty; everyone else's penalty and boost fade.
//
// With dynamic: false, the score is just the base priority (the "before" case).
(function (global) {
  function simulate(tasks, options = {}) {
    const dynamic = options.dynamic !== false;
    const maxTicks = options.maxTicks || 100;

    const procs = tasks.map(t => ({
      name: t.name,
      type: t.type === 'io' ? 'io' : 'cpu',
      base: Number(t.base) || 0,
      burst: Number(t.burst) || 1,
      remaining: Number(t.burst) || 1,
      aging: 0,
      penalty: 0,
      boost: 0,
      blockedFor: 0,
      sinceBlock: 0,
      waitTicks: 0,
      finishTick: null
    }));

    const score = p => (dynamic ? p.base + p.aging + p.boost - p.penalty : p.base);
    const timeline = [];

    for (let tick = 1; tick <= maxTicks; tick++) {
      if (procs.every(p => p.remaining === 0)) break;

      // I/O tasks that finished waiting come back with a boost
      procs.forEach(p => {
        if (p.blockedFor > 0) {
          p.blockedFor--;
          if (p.blockedFor === 0 && dynamic) p.boost = 3;
        }
      });

      // Pick the ready task with the highest score (ties go to the name, alphabetically)
      let runner = null;
      procs.forEach(p => {
        if (p.remaining === 0 || p.blockedFor > 0) return;
        if (!runner || score(p) > score(runner) || (score(p) === score(runner) && p.name < runner.name)) {
          runner = p;
        }
      });

      // Ready tasks that did not run wait one more tick (and age if dynamic)
      procs.forEach(p => {
        if (p === runner || p.remaining === 0 || p.blockedFor > 0) return;
        p.waitTicks++;
        if (dynamic) p.aging = Math.min(p.aging + 1, 20);
      });

      if (runner) {
        runner.remaining--;
        runner.sinceBlock++;
        if (dynamic) {
          runner.aging = 0;
          runner.boost = 0;
          runner.penalty = Math.min(runner.penalty + 2, 10);
        }
        if (runner.remaining === 0) {
          runner.finishTick = tick;
        } else if (runner.type === 'io' && runner.sinceBlock >= 2) {
          runner.blockedFor = 2; // waiting for input
          runner.sinceBlock = 0;
        }
      }

      if (dynamic) {
        procs.forEach(p => {
          if (p !== runner) {
            p.penalty = Math.max(0, p.penalty - 1);
            p.boost = Math.max(0, p.boost - 1);
          }
        });
      }

      timeline.push({ tick, running: runner ? runner.name : null });
    }

    return {
      timeline,
      summary: procs.map(p => ({
        name: p.name,
        type: p.type,
        base: p.base,
        burst: p.burst,
        waitTicks: p.waitTicks,
        finishTick: p.finishTick
      }))
    };
  }

  global.Scheduler = { simulate };
})(window);
