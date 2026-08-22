# CoverPoints — Documentation

**Cover point generation, constant-time scoring and exclusive reservation for Unreal Engine 5.8.**

---

## Table of contents

1. [What this plugin is, and what it is not](#1-what-this-plugin-is-and-what-it-is-not)
2. [Installation](#2-installation)
3. [Concepts](#3-concepts)
4. [Quick start](#4-quick-start)
5. [`ACoverVolume` — where cover is generated](#5-acovervolume--where-cover-is-generated)
6. [Generation: how a point is measured](#6-generation-how-a-point-is-measured)
7. [Scoring: how a query picks one](#7-scoring-how-a-query-picks-one)
8. [Reservation: one point, one agent](#8-reservation-one-point-one-agent)
9. [Verification: catching a point that lies](#9-verification-catching-a-point-that-lies)
10. [Blueprint API](#10-blueprint-api)
11. [C++ API](#11-c-api)
12. [Project settings](#12-project-settings)
13. [The statistics box](#13-the-statistics-box)
14. [Console commands](#14-console-commands)
15. [Performance](#15-performance)
16. [Recipes](#16-recipes)
17. [Troubleshooting](#17-troubleshooting)
18. [Scope and limits](#18-scope-and-limits)

---

## 1. What this plugin is, and what it is not

**It is** the layer underneath a cover system: it turns level geometry into a list of places worth
standing, indexes them, scores them against a threat in constant time, and guarantees that no two agents
are ever given the same one.

**It is not** cover behaviour. There is no Behavior Tree node, no animation, no montage, no state
machine, no agent that walks itself into a point and leans out of it. It answers *where* and *whose*.
What an agent does when it arrives is your game's business.

That separation is deliberate and it is the reason the plugin is small. A cover *behaviour* has to know
about your animation graph, your weapon code, your damage model and your movement component. A cover
*index* has to know about none of them, which is why it can be dropped into a project on a Tuesday and
be answering queries the same afternoon.

---

## 2. Installation

1. Copy the `CoverPoints` folder into your project's `Plugins` directory.
2. Open the project. If asked to rebuild, say yes.
3. *Edit → Plugins → AI → CoverPoints* — make sure it is enabled, and restart if prompted.

The plugin's module loads at `PreDefault`, so the subsystem, the volume class and the `Cover.*` console
commands all exist before the first game world is created. A volume in a map that is loaded on startup can
therefore begin generating on the very first frame.

---

## 3. Concepts

### Cover point

A place an agent can stand, plus the outward surface normal of the geometry that shields it. That normal
is the whole design in one field: **it points away from the wall and out into the open**, so a threat is
blocked exactly when the direction from the point to the threat is opposite to it. One dot product, and
that is the entire definition of cover here.

### High and low

Measured, not authored. A second sweep at standing height in the same direction as the one that found the
cover. Still blocked → `High`: the geometry is taller than the agent and standing behind it is enough.
Passes through → `Low`: the agent has to crouch, and the same miss is what makes shooting over the top
possible.

### Peek sides

`bCanPeekLeft`, `bCanPeekRight`, `bCanPeekOver`. Each lateral side costs two sweeps: one that the agent
can *reach* the leaning position, and one that from there it can *see past* the cover. Checking only the
second would happily report a peek through a wall the agent cannot lean into.

### Handle

`FCoverPointHandle` is an index plus a generation counter. Points live in one flat array that is replaced
whenever a volume is rebuilt, so a bare index handed out before a rebuild would silently address a
*different* point afterwards — the worst kind of bug, because it looks like it works. The generation
counter makes a stale handle fail `IsCoverValid` instead of lying.

### Threat

Two different things share the word:

- The threat position a **query** is scored against. Passed per call. One fight can have four threats and
  each agent can score against its own.
- The world's **current** threat, set with `SetThreatLocation` / `SetThreatActor`. It drives the debug
  colours and the background verifier only. Queries never read it.

---

## 4. Quick start

1. Place a **Cover Volume** and scale its box over the area you want cover in.
2. Press Play. It builds on `BeginPlay`, in slices, under the millisecond budget.
3. In an AI Blueprint:

```
Event Wants Cover
  └─ Find Best Cover
       World Context : self
       Agent Actor   : self
       Threat Location : (the player's location)
       Search Radius : 2000
       Claim Cover   : true
     ├─ Branch on Result.bFound
     │    └─ AI Move To → Result.Point.Location
     └─ (else) fall back to whatever you already do when there is no cover
```

4. When the agent leaves cover, dies, or picks a new target: **Release Cover For Actor**.

That is the whole integration. Everything below is detail.

---

## 5. `ACoverVolume` — where cover is generated

A box component, not an `AVolume` with a brush. A brush is an editor-authored asset: it cannot be spawned
at runtime with a sensible size, it cannot be scaled from a Blueprint, and a procedurally generated level
therefore cannot lay one down over the room it just built. A box can do all three, and everything this
class needs from its shape is an oriented extent.

Everything on it is a property of the **place**, not of the project. A warehouse of shipping containers
and a trench line want different numbers, and they are different volumes.

| Property | Default | What it means |
|---|---|---|
| `GridSpacing` | 200 cm | Distance between floor samples. Cost and point count are **quadratic** in this: halving it gives four times both. |
| `AgentRadius` | 34 cm | Radius of the sweep sphere. A gap narrower than the agent is never reported as a way past a wall. |
| `CoverDistance` | 140 cm | How far from geometry a sample may stand and still count as cover. A standoff, not a search radius. |
| `CrouchHeight` | 60 cm | Height the low sweep fires from. This is the sweep that decides cover exists at all. |
| `StandHeight` | 140 cm | Height the high sweep fires from. Blocked here → `High`. |
| `bBuildOnBeginPlay` | true | Off for levels that assemble themselves first; call `RebuildVolume` when the geometry has stopped moving. |
| `bVolumeEnabled` | true | Off keeps the volume registered and its points gone — for a region sealed off until an act of the game opens it. |
| `IgnoredActors` | — | Things that are solid but are not cover: a clip-brush cage, a physics prop that will be gone, the agents themselves. |

The grid is laid out in the box's **local** space and centred, so a rotated volume laid along a ramp
samples the ramp, and a volume nudged 30 cm sideways does not resample the whole room in a different
place.

The box has collision disabled and `bCanEverAffectNavigation` off. If it affected navigation it would
carve a hole in the navmesh under itself and then reject every sample it laid down for failing to project
onto the navmesh it had just removed.

---

## 6. Generation: how a point is measured

Per grid sample, in order. Any step that fails ends the sample.

1. **Floor.** A line trace from the ceiling of the volume straight down its own local Z. No hit, or a
   surface normal shallower than `MinFloorNormalZ` (0.7, about 45°), and the sample is dropped — a cover
   point on a slide is a cover point the agent is pushed out of a second after it arrives.

2. **Reachability.** If `bProjectToNavMesh` is on, the floor position is projected onto the navmesh
   within `NavProjectionExtent`. Failing that, the sample is dropped: a point an agent cannot reach is
   not cover, it is a decoration queries will keep offering. When the world has no navigation system at
   all this step is skipped, so a test map without a navmesh still generates something.

3. **Walls.** Eight horizontal sphere sweeps of `AgentRadius` at crouch height, at 45° apart, out to
   `CoverDistance`. Each hit contributes a candidate carrying the **flattened** impact normal — cover is a
   horizontal idea, and a hit whose normal is essentially vertical is a floor or ceiling the sweep grazed.
   If the sphere starts penetrating, the sample is inside geometry and is dropped whole.

4. **Distinct normals.** One flat wall is hit by three of the eight sweeps and must produce **one** point.
   A corner genuinely is two pieces of cover facing two ways and should produce **two**. Candidates whose
   normals have a dot product above `DistinctNormalThreshold` (0.9, about 25°) are merged, closest hit
   wins, and at most `MaxPointsPerSample` (2) survive.

5. **Height.** The same sweep from `StandHeight`. Blocked → `High`, clear → `Low`.

6. **Peek sides.** Per lateral side: a sweep from the point to the leaning position
   (`PeekLateralOffset`, 85 cm), then a sweep from there towards the wall for `PeekProbeDistance`
   (600 cm). Both clear → that side is peekable. Over the top: one sweep from `StandHeight` towards the
   wall. High cover fails this by construction — it is the same line that made it High — so no special
   case is written for it.

### The slicing

`TickBuild` runs until **either** `MaxSamplesPerFrame` samples have been processed **or**
`MaxBuildMillisecondsPerFrame` has elapsed, whichever comes first. The clock is checked every 16 samples
rather than every one: `FPlatformTime::Seconds` is not free, and a check that costs a measurable slice of
the budget it guards has become the cost.

Progress is a real fraction. The sample count of every volume is worked out up front, so the percentage
on the box is measured, not estimated, and a volume destroyed mid-build is skipped whole with its samples
still counted — the percentage cannot stall at 63 for the rest of the level.

Requesting a build while one is running **replaces** it rather than queueing behind it. The newer request
is by definition the more recent description of the level; finishing the older one first would publish
points the caller has already said are wrong.

### Partial rebuilds

`RebuildVolume(SomeVolume)` regenerates only that volume. Points belonging to volumes the call did not
name survive it, and their claims survive with them — rebuilding the room a wall just came down in must
not throw away the cover in the rest of the level, nor take an agent's wall away in a corridor nothing
happened in.

All handles cut before a rebuild become invalid, because indices move. Agents should re-query; that is
what `IsCoverValid` is for.

---

## 7. Scoring: how a query picks one

No traces. The cells of the spatial hash overlapping the search sphere are gathered and every point in
them is scored on six terms, each normalised to 0..1:

| Term | What it measures |
|---|---|
| **Shielding** | `-dot(CoverNormal, dirToThreat)`, remapped to 0..1. The term that means "cover". |
| **Agent distance** | 1 at the agent's feet, 0 at the edge of the search radius. |
| **Threat distance** | 1 at `PreferredThreatDistance`, falling off in **both** directions. |
| **High cover** | 1 for `High`, 0 for `Low`. |
| **Peek** | 1 when the point can lean towards the threat, 0 otherwise. |
| **Wall proximity** | 1 pressed against the wall, 0 at the volume's full cover distance. |

The weighted sum is divided by the sum of the weights, so a score is a number between 0 and 1 that means
the same thing in two different levels — instead of a number whose scale is an accident of how the
weights happened to be tuned. Doubling all six weights changes nothing, which is the property that makes
them safe to tune by feel.

Three filters run before scoring: `MinShielding` (points below it are not returned at all, not merely
scored badly), `bRequireHighCover`, and `bRequirePeek`. Points that are invalidated, or claimed by
somebody else, are skipped — unless `bAllowClaimed` is on, which is for asking questions about a level
rather than sending anybody anywhere.

### `PreferredThreatDistance` is the one number worth tuning

It is not "as far from the threat as possible". A point too far away is as wrong as one too close,
because an agent that retreats out of its own weapon range stops being a threat and starts being scenery.
Set it to the effective range of whatever the agent is carrying, and the same cover field becomes a
shotgun layout or a rifle layout without a single point being regenerated.

---

## 8. Reservation: one point, one agent

`ClaimCover` takes a point or fails. There is no queue, no priority, no sharing.

- Claiming a point the **same** agent already holds succeeds and pushes the expiry out. That is how an
  agent that sits in cover for two minutes keeps it, without the caller needing to know expiries exist.
- Claiming a new point releases the agent's previous one in the same call. An agent that moves from cover
  to cover and forgets to release would otherwise hoard the level.
- Every claim carries an expiry (`ClaimLifetimeSeconds`, 30 s by default). The reason it exists: an agent
  destroyed while holding a point cannot release it, and cover permanently held by a corpse is a level
  that runs out of cover over an afternoon. A sweep every `ClaimSweepInterval` collects claims whose
  holder is gone or has stopped refreshing.

### Claim inside the query

`Find Best Cover` has a **Claim Cover** flag, on by default. Leave it on.

Claiming inside the query is what makes it impossible for two agents that query on the same frame to be
handed the same point. Claiming a frame later, from the Blueprint, is not — between the two agents' query
nodes there is a window where the point is still free, and both will walk to it.

This is the reason the plugin exists as a service rather than as a helper function.

---

## 9. Verification: catching a point that lies

Generation happens once. Levels do not. A door opens, a wall is blown out, a container is driven away —
and a point that was measured as safe is now a lie.

Every frame, at most `MaxVerificationTracesPerFrame` (4) line traces are spent auditing points against
the world's current threat. A point only costs a trace if it *claims* to block: shielding below
`VerificationMinShielding` is skipped, because a point that never claimed to block cannot be caught lying
about blocking, and auditing it would spend the budget confirming things nobody asserted.

When the trace reaches the threat unobstructed, the point is invalidated: it stops being returned by
every query, its claim is released, `OnPointInvalidated` fires, and the counter on the box goes up. A
rebuild brings it back if the geometry did.

The sweep is bounded twice — by the trace budget, and by never walking the point array more than once per
frame. Without the second bound, a level where nothing passes the shielding filter would spin the cursor
through every point every frame looking for work it is never going to find.

---

## 10. Blueprint API

All of it is on **UCoverPointsStatics**, category *CoverPoints*. Every call is world-context-aware and
safe in a world with no CoverPoints subsystem at all: queries answer "no cover", setters do nothing,
nobody crashes. An AI Blueprint written against this plugin still runs in a test map where no volume was
ever placed — it takes the fallback branch the designer already wrote.

### The five an AI actually makes

| Node | Returns | Notes |
|---|---|---|
| **Find Best Cover** | `FCoverQueryResult` | Agent actor, threat location, radius, claim flag, optional params. |
| **Claim Cover** | `bool` | False when somebody else holds it. |
| **Release Cover** | `bool` | False when this agent did not hold it. |
| **Release Cover For Actor** | `int32` | No handle needed. Call this on death. |
| **Is Cover Valid** | `bool` | False after a rebuild, and after the verifier struck the point out. |

### The rest

| Node | Purpose |
|---|---|
| **Find Best Cover At Location** | The same query without an agent actor. Cannot claim. |
| **Find Cover Near** | Every point in a sphere, nearest first, threat ignored. |
| **Release All Cover** | Free every claim in the world. |
| **Rebuild Volume** | One volume, or every enabled volume when null. |
| **Finish Build Immediately** | Run the queued build to completion this frame. Loading screens, not gameplay. |
| **Clear All Cover** | Points, claims and the build in flight. |
| **Is Building Cover** / **Get Cover Build Progress** | For a loading bar. |
| **Set Threat Location** / **Set Threat Actor** / **Clear Threat** / **Get Threat Location** | The world's current threat, for the colours and the verifier. |
| **Get Cover Point** / **Get Cover Owner** / **Get Claimed Cover For Actor** / **Get Cover Point Count** | Inspection. |
| **Get Cover Stats** | Everything the box draws, as a struct. |
| **Get All Cover Volumes** | Every volume in the world. |
| **Get Cover Shielding** | 1 fully behind the cover, −1 fully exposed. |
| **Get Peek Side Towards** | Which way to lean, given what the point can actually do. |
| **Get Cover Facing Rotation** | Out into the open, **not** into the wall. |
| **Peek Side To String** | For a label on a screen. |
| **Set Show Cover Points** / **Set Show Peek Sides** | Debug drawing. |

### Events

On the subsystem: `OnBuildCompleted(int32 TotalPoints)` and `OnPointInvalidated(FCoverPointHandle)`.

---

## 11. C++ API

```cpp
#include "CoverPointsSubsystem.h"
#include "CoverPointsStatics.h"
#include "CoverVolume.h"

UCoverPointsSubsystem* Cover = UCoverPointsSubsystem::Get(this);
if (!Cover)
{
    return;   // no cover system in this world, and that is a legitimate world
}

FCoverQueryParams Params;
Params.PreferredThreatDistance = 900.0f;   // this agent carries a shotgun
Params.bRequireHighCover       = true;

const FCoverQueryResult Result = Cover->FindBestCover(
    /* AgentActor    */ MyPawn,
    /* AgentLocation */ MyPawn->GetActorLocation(),
    /* ThreatLocation*/ Target->GetActorLocation(),
    /* SearchRadius  */ 2500.0f,
    /* Params        */ Params,
    /* bClaimAgent   */ true);

if (Result.bFound)
{
    MoveTo(Result.Point.Location);
    FaceTowards(UCoverPointsStatics::GetCoverFacingRotation(Result.Point, Target->GetActorLocation()));
    // Result.PeekSide tells the animation which way to lean.
}
```

On death or disengagement:

```cpp
Cover->ReleaseCoverForActor(MyPawn);
```

`FindBestCover` is the only non-`UFUNCTION` entry point on the subsystem, because its argument list is
longer than a Blueprint node should be. Everything else is reflected.

---

## 12. Project settings

*Project Settings → Plugins → CoverPoints*. These are what a project decides once — what a *place* looks
like is on the volume.

### General

| Setting | Default | What it does |
|---|---|---|
| `bEnabled` | true | Off leaves the subsystem running and generates nothing. For bisecting a frame cost without pulling volumes out of a map. |

### Generation

| Setting | Default | What it does |
|---|---|---|
| `GenerationChannel` | `Visibility` | Channel the sweeps trace on. |
| `bTraceComplex` | false | Simple shapes are what cover wants. |
| `MaxSamplesPerFrame` | 192 | Sample ceiling per frame. |
| `MaxBuildMillisecondsPerFrame` | 2.0 ms | Time ceiling per frame. Whichever is reached first ends the slice. |
| `bProjectToNavMesh` | true | Drop samples an agent cannot reach. |
| `NavProjectionExtent` | 120 cm | How far a sample may be moved to land on the navmesh. |
| `MinFloorNormalZ` | 0.7 | Steepest floor a point may sit on. |
| `DistinctNormalThreshold` | 0.9 | How different two normals must be for one sample to yield two points. |
| `MaxPointsPerSample` | 2 | Ceiling on points per grid sample. |
| `MinPointSeparation` | 0 cm | Thinning. **Off by default** so that halving the grid really does give four times the points. |

### Peeking

| Setting | Default | What it does |
|---|---|---|
| `PeekLateralOffset` | 85 cm | How far sideways an agent is assumed to lean. A body width, not a step. |
| `PeekProbeDistance` | 600 cm | How far past the cover the probe looks. Must comfortably exceed the volume's cover distance. |

### Queries

| Setting | Default | What it does |
|---|---|---|
| `QueryCellSize` | 500 cm | Spatial hash cell edge. The one number that decides what a query costs. |
| `MaxCandidatesPerQuery` | 512 | Ceiling, not a target. Truncation is never silent — the box prints what was scored. |
| `DefaultQueryParams` | — | The scoring defaults every query starts from. |

### Claims

| Setting | Default | What it does |
|---|---|---|
| `ClaimLifetimeSeconds` | 30 s | How long an unrefreshed claim survives. |
| `ClaimSweepInterval` | 0.25 s | How often expired and orphaned claims are collected. |

### Verification

| Setting | Default | What it does |
|---|---|---|
| `bVerifyCover` | true | Spot-check points against the current threat. |
| `MaxVerificationTracesPerFrame` | 4 | Small on purpose. This is a background audit, not a per-query check. |
| `VerificationMinShielding` | 0.3 | Only audit points that claim to block. |
| `VerificationEyeHeight` | 60 cm | Height the audit trace starts from. |

### Presentation

| Setting | Default | What it does |
|---|---|---|
| `bShowStatsByDefault` | true | Start with the counters box on. |
| `bAutoDrawStatsOnAnyHUD` | false | Draw the box through `AHUD::OnHUDPostRender`, so a project keeps its own HUD class. |
| `bShowPointsByDefault` | false | Equivalent to `Cover.Show 1` at startup. |
| `bShowPeekSidesByDefault` | false | Adds the lean markers. |

---

## 13. The statistics box

Canvas, drawn from `AHUD`, thirteen lines. Canvas rather than UMG for two reasons pulling the same way:
it has to survive a cooked Shipping build, where `DrawDebug` is compiled out and a debug widget is
usually stripped; and anything that has to be **clicked** belongs in UMG instead, because an `AHUD` hit
box is tested against the viewport's mouse position, which reports nothing at all on a machine with no
mouse attached.

| Line | What it proves |
|---|---|
| `Points` | Total, and the high/low split. |
| `Peekable` | How many offer at least one way out. |
| `Build` | A progress bar and a percentage. |
| `Samples` | Processed against total, so the bar can be checked against a real fraction. |
| `Build ms/f` | Milliseconds this frame, the peak, **and the configured budget beside it**. A budget printed without the cost next to it proves nothing. |
| `Last build` | Wall-clock milliseconds for the whole last build, slices and gaps included. |
| `Queries` | Per second, and the lifetime total. |
| `Per query` | **Microseconds**, averaged over the last second, and the most recent one. |
| `Candidates` | How many points the hash handed the scorer, against how many a linear search would have touched. This is the evidence for the line above it. |
| `Claimed` | Points held right now, and how many claims have expired. |
| `Verify` | Spot-check traces fired, and points struck out by them. |
| `Threat` | Where the current threat is, and how many volumes are registered. |

Two draw paths: set the map's HUD class to `ACoverPointsHUD`, or turn on `bAutoDrawStatsOnAnyHUD` and
keep your own. They know about each other and cannot stack.

---

## 14. Console commands

| Command | What it does |
|---|---|
| `Cover.Build [now]` | Generate for every enabled volume. `now` skips the per-frame budget and finishes on this frame. |
| `Cover.Stats` | Print the measured counters to the log. |
| `Cover.Show [0\|1] [peek]` | Draw the points, coloured against the current threat. `peek` adds the lean markers. |
| `Cover.Threat [X Y Z \| player \| off]` | Set what the colours and the spot-checks measure against. No arguments prints the current one. |
| `Cover.Clear` | Throw away every point, every claim and any build in flight. |

---

## 15. Performance

### Generation

Per grid sample: 1 line trace down, up to 8 sphere sweeps around, and per surviving candidate 1 sweep for
height plus up to 5 for the peek sides. A 40 × 40 m volume at 200 cm spacing is 441 samples; at 100 cm it
is 1681. Cost is quadratic in the spacing and it is paid once, spread over frames, under a ceiling that
is printed on screen next to what it actually cost.

### Queries

A query is: a range of hash cells, a bounds test per point in them, and six multiply-adds for the ones
that pass. No allocation, no trace, no sort. `QueryCellSize` is the number that decides it — a cell
roughly the size of a typical search radius keeps both halves small. Too small and a query walks hundreds
of nearly empty cells; too large and each cell hands the scorer points from the next room.

**The claim on the box.** Double the point count by halving `GridSpacing` and the *candidate* count for
a fixed search radius rises with the density of that radius, not with the size of the level — which is
what "constant time" means here, and why the candidate count is printed next to the microseconds.

A search radius so large that walking its cells would cost more than walking the points degrades to a
linear scan, still bounded by `MaxCandidatesPerQuery`. It degrades; it does not stall.

### Everything else

One tick for the whole world. The claim sweep is O(claims), which is the small number, and runs four
times a second. Verification is a fixed trace budget. Debug drawing is O(points) per frame and is
compiled out of Shipping entirely.

---

## 16. Recipes

### Send a squad to eight different points

```
For Each  (Agents)
  └─ Find Best Cover  (Agent, Threat, Radius 3000, Claim Cover = true)
       └─ Branch bFound → AI Move To  Result.Point.Location
```

Nothing else is needed. The claim happens inside the query, so the second agent cannot be handed the
first agent's point, and the box will read `Claimed 8`.

### Rebuild after a wall comes down

```
On Wall Destroyed
  └─ Rebuild Volume  (the volume covering that room)
```

The rest of the level keeps its points and its claims. Agents holding handles in the rebuilt room will
see `Is Cover Valid` go false and should re-query; the throttled verifier catches the same situation
without a rebuild at all, a few frames later, and invalidates the affected points on its own.

### A loading-screen build

```
Level Loaded
  └─ Rebuild Volume (null)          // every enabled volume
  └─ Finish Build Immediately        // no budget, one frame
```

Only do this behind a loading screen. That is what the budget exists to avoid the rest of the time.

### Different weapons, one cover field

```
FCoverQueryParams Shotgun;  Shotgun.PreferredThreatDistance = 600;
FCoverQueryParams Rifle;    Rifle.PreferredThreatDistance   = 2000;
```

Same points, same index, same microseconds. Two different layouts.

### Prefer flanking cover

```
Params.PeekWeight       = 3.0;
Params.ShieldingWeight  = 1.5;
Params.MinShielding     = 0.0;   // side-on to the threat is acceptable
```

---

## 17. Troubleshooting

**No points at all.**
Check the box: is `Samples` non-zero? If it is zero the volume's box has no floor under it, or
`bVolumeEnabled` is off, or `bEnabled` is off in project settings. If samples are being processed but no
points appear, the geometry is further than `CoverDistance` from every sample, or `bProjectToNavMesh` is
on and the level has a navmesh that does not cover the area — build navigation, or turn the projection
off.

**Points only along one wall.**
`CoverDistance` is short relative to `GridSpacing`. A sample only becomes a point if it can reach geometry
within the standoff, so a 200 cm grid with a 60 cm standoff will miss most of a room.

**Every point says it cannot peek.**
`PeekProbeDistance` is shorter than the volume's `CoverDistance`, so the probe stops inside the wall.
Raise it — the default 600 cm comfortably clears the default 140 cm standoff.

**Two agents at the same point.**
Only possible if `bClaimCover` was off in the query, or `bAllowClaimed` was on in the params. Both are
opt-in for exactly this reason.

**Points invalidate that should not.**
The verifier only strikes out a point when a clear line reaches the current threat. If the current threat
is set somewhere silly — inside geometry, at the world origin — everything near it will look exposed.
`Cover.Threat` with no arguments prints where it thinks the threat is.

**The build stutters.**
Lower `MaxSamplesPerFrame` or `MaxBuildMillisecondsPerFrame`. The box prints both the cost and the budget;
if the cost is far above the budget, a single *sample* is expensive — usually complex collision on very
dense geometry. Turn `bTraceComplex` off.

**Handles keep going invalid.**
Something is rebuilding. Every rebuild moves indices and bumps the generation counter. Do not rebuild on a
timer; rebuild when the level changes.

---

## 18. Scope and limits

**One runtime module.** `LoadingPhase: PreDefault`. `Core`, `CoreUObject`, `Engine`, `DeveloperSettings`,
`NavigationSystem`; `RenderCore` privately for the one-pixel texture the counters box is tiled from.

**No UMG in the product.** The counters box is Canvas. Demo buttons are UMG assets in Content that call
the Blueprint library, exactly as a project would.

**No Niagara, no Chaos, no editor module.** Everything here ships, so nothing can go missing between what
a designer places in the editor and what the packaged game generates.

**No network replication.** Cover points are derived from geometry that is already replicated by being
part of the level; every machine generates the same set from the same geometry. Claims are authoritative
on whichever machine runs the AI, which in the ordinary case is the server. Replicating the claim map is
a game-specific decision and is deliberately left to the game.

**Game and PIE only.** `DoesSupportWorldType` excludes editor worlds. Generating cover into a viewport
while somebody is still moving the walls around is the sort of help nobody asked for.

**Invalidation is one-way.** A point struck out by the verifier stays struck out until a rebuild. Bringing
it back would need a second audit pass proving the geometry returned, and a point that flickers between
usable and not is worse than one that is honestly gone.

**Built and verified on Win64.** Mac and Linux are enabled in the plugin descriptor but were not built for
this release.

---

© 2026 Silvan Teufel. All rights reserved.
