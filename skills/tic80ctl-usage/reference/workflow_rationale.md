# Why This Workflow Exists

When using `tic80ctl`, prefer this loop:

1. write or update the cart
2. run one bounded episode or command
3. inspect the resulting evidence
4. revise
5. repeat

The point is to learn from one reproducible experiment at a time.

## Prefer Bounded Experiments

Do not default to lots of tiny manual actions when one short scripted episode can answer the question.

Usually the right abstraction is:

- run one route
- inspect one artifact directory
- compare the result to the previous run

not:

- poke the game repeatedly
- issue many tiny commands
- try to reconstruct what happened from scattered observations

## Why This Is Better

This workflow helps because it:

- reduces unnecessary control churn
- makes runs easier to repeat
- keeps evidence in one place
- makes before/after comparison easier
- makes failures easier to localize

If you rerun the same script after a code change, you get a meaningful comparison instead of a vague impression.

## What Counts As Evidence

Good evidence usually includes:

- the exact script that ran
- route labels in `log.txt`
- cart-side facts in `console.txt`
- screenshots from the relevant frames
- a clear end status and message

That is usually enough to decide what to fix next.

## Practical Rule

When choosing between:

- many small exploratory actions
- one short planned run with saved artifacts

prefer the second option unless you are debugging a very narrow one-off detail.

Use the shell commands for setup and targeted probing.
Use `playtest` for anything that depends on multiple frames, route flow, or proof of behavior over time.
Each `playtest` starts from a fresh cart restart, which keeps comparisons reproducible across reruns.

## Working Style

Treat TIC-80 iteration as an optimization loop:

- form a concrete hypothesis
- run a focused experiment
- inspect the result
- revise based on evidence

That keeps the work legible and prevents blind trial-and-error.
