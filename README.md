# CoverPoints

**Cover point generation, constant-time scoring and exclusive reservation, for Unreal Engine 5.8.**

The cover in your level already exists — it is just written down nowhere. Drop a **Cover Volume** over an
area, and CoverPoints finds it once, scores it in constant time against any threat you name, and hands
every point to exactly one agent.

No Behavior Tree. No animation. No agent that walks itself into cover. **This is the service underneath
one of those**, and it leaves the walking and the leaning to your character code.

---

## The part that matters: generation costs traces, queries do not

The version everybody writes by hand shoots a line trace per candidate per query. It gets slower every
time an artist adds a crate, and slower again every time a designer adds an agent.

Here, the sweeps that decide what a point *is* — one down for the floor, eight around for the wall, one at
standing height, up to five more for the ways out of it — are paid **once**, in slices, under a
millisecond ceiling per frame. Afterwards a query is a spatial-hash lookup over a few cells and a handful
of dot products. It costs the same whether the level holds two hundred points or twenty thousand, and the
statistics box prints the microseconds and the candidate count so you never have to take that on trust.

## What it does

- **Finds cover by measuring it.** A grid over the volume's floor; eight horizontal sweeps per sample.
  Geometry inside the cover distance becomes a point carrying the **surface normal** of the thing that
  shields it. A corner produces two points facing two ways; a flat wall produces one.
- **High or low, measured.** A second sweep at standing height. Still blocked → the point hides a standing
  agent. Not blocked → the agent has to crouch, and can shoot over the top.
- **Peek sides, measured.** Two sweeps per side: one that the agent can reach the leaning position at all,
  one that from there it can see past the cover. `PeekLeft`, `PeekRight` and `PeekOver` are facts about
  the level, not guesses from a bounding box.
- **Generation in slices.** At most *N* grid samples and *M* milliseconds per frame, with a live
  percentage. A level that freezes for two seconds while it builds is not a shipping feature.
- **Constant-time scoring.** Angle between cover normal and threat direction, distance to the agent,
  distance to the threat, high against low, which way it can peek, how close it hugs the wall. Six
  weights, no traces.
- **Exclusive reservation.** `Claim` / `Release` with an expiry. Eight agents sent at once end up at eight
  different points; two never share one — including when the agent that held a point died without
  releasing it.
- **A throttled verifier.** A small fixed budget of line traces per frame spot-checks that points scored
  as safe really block. One that lies is invalidated on the spot and the counter says so.
- **Numbers you can read.** A Canvas statistics box — points generated and how many are high or low, build
  progress and milliseconds per frame, queries per second, **microseconds per query**, points claimed,
  spot-checks run and points invalidated by them. It survives a Shipping build.

## What it is not

It is not cover *behaviour*. There is no Behavior Tree node, no animation, no montage, no agent that
walks itself into a point and leans out of it. CoverPoints answers **where** and **whose**; what the agent
does when it gets there is your game's business, and it always was.

## Quick start

1. Enable the plugin and restart the editor.
2. Place a **Cover Volume** in the level and scale its box over the area you want cover in. Leave
   `GridSpacing` at 200 to start with.
3. Press Play. The volume builds on BeginPlay, in slices.
4. From an AI Blueprint, call **Find Best Cover** with the agent, the threat's location and a search
   radius. Leave *Claim Cover* on. Walk the agent to `Result.Point.Location`.
5. Call **Release Cover For Actor** when the agent leaves cover or dies.

To see the numbers, set the map's HUD class to **Cover Points HUD** — or, if the project already has a
HUD, turn on *Project Settings → Plugins → CoverPoints → Auto Draw Stats On Any HUD*.

To see the points, run `Cover.Show 1 peek` and `Cover.Threat player`.

## Demo map

`/CoverPoints/CoverPoints/Maps/L_CoverPointsDemo` — an arena of walls, pillars and crates, one Cover
Volume, eight agents and a movable threat. Press Play and use the panel top-right: **1** builds the cover
field, **2** sends eight agents to eight different points, **3** moves the threat and re-queries, **4**
gives every claim back. The statistics box top-left is the plugin's own, not the demo's.

## Console commands

| Command | What it does |
|---|---|
| `Cover.Build [now]` | Generate cover for every enabled volume. `now` skips the per-frame budget. |
| `Cover.Stats` | Print the measured counters to the log. |
| `Cover.Show [0\|1] [peek]` | Draw the points, coloured against the current threat. `peek` adds the lean markers. |
| `Cover.Threat [X Y Z \| player \| off]` | Set what the colours and the spot-checks are measured against. |
| `Cover.Clear` | Throw away every point, every claim and any build in flight. |

## Classes

| Class | What it is |
|---|---|
| `ACoverVolume` | The area cover is generated in, and the body measurements it is generated for. |
| `UCoverPointsSubsystem` | Generation, spatial index, scoring, reservation and the counters. Game and PIE. |
| `FCoverPoint` | A place to stand, the normal of what shields it, high/low, peek sides, owner, score. |
| `UCoverPointsStatics` | The whole plugin from Blueprint, with no subsystem reference in sight. |
| `UCoverPointsSettings` | Project defaults, under *Project Settings → Plugins → CoverPoints*. |
| `ACoverPointsHUD` | The Canvas statistics box. |

## Scope

One runtime module, `LoadingPhase: PreDefault`. Depends on `Core`, `CoreUObject`, `Engine`,
`DeveloperSettings` and `NavigationSystem` — the last only to project grid samples onto walkable floor.

No UMG in the product, no Niagara, no Chaos, no editor module, no network replication.

Built and verified on **Win64**. Mac and Linux are enabled in the plugin descriptor but were not built for
this release.

## Documentation

Full reference in [`Docs/DOCUMENTATION.md`](Docs/DOCUMENTATION.md).

---

© 2026 Silvan Teufel. All rights reserved.

<!-- SF-STORE-BLOCK:BEGIN -->
## 🛒 Source-available — see before you buy

This repository contains the **full source** of a commercial Unreal Engine plugin. It is **source-available, not open source**: read it, evaluate it, then buy a license to use it. See **the Fab Content License Agreement / Unreal Engine EULA (purchase required)**.

**Get it / Buy:**
- Fab store — all our UE5 plugins: https://www.fab.com/sellers/Silvan%20Teufel

_This plugin does not have its own Fab listing yet — the store link above is where everything we currently sell lives._

### 📬 **Free UE5 Snippet-Pack**

10 ready-to-use C++/Blueprint building blocks (subsystems, versioned saves, async nodes, editor tooling) — MIT licensed. Get it by joining the newsletter — plus a heads-up when something new ships. Double opt-in, unsubscribe in one click, no address sharing.

👉 **[Get the free pack](https://silvan.teufel-engineering.com/newsletter/plugins/?q=gh)**

_© 2026 Silvan Teufel. All rights reserved._
<!-- SF-STORE-BLOCK:END -->
