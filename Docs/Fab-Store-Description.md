# CoverPoints — Fab Store Description

**Title:** CoverPoints - Cover Point Generation, Scoring and Exclusive Reservation
**Price:** $14.99
**Category:** AI
**Engine:** 5.8
**Licences:** Personal and Professional

---

## Short description

The cover in your level already exists — it is just written down nowhere. CoverPoints finds it once with
sweeps against the geometry, scores it in constant time against any threat, and hands every point to
exactly one agent. Not cover behaviour: the service underneath one.

---

## Long description

### Cover is already in your level

Every crate, low wall, doorframe and pillar an artist placed is a piece of cover. None of it is written
down anywhere an AI can read.

Drop a **Cover Volume** over an area. A grid is laid across its floor; each sample fires eight horizontal
sweeps, and geometry found inside the cover distance becomes a cover point carrying the **surface normal**
of the thing that shields it. A second sweep at standing height decides **high** cover against **low**.
Three more decide whether an agent can lean out **left**, **right** or **over the top**.

Nothing is authored. Nothing is placed by hand. Move a container two metres and rebuild that volume; the
cover moves with it.

### Generation costs traces. Queries do not.

This is the whole design, and it is what separates the plugin from the version everybody writes by hand.

The hand-written one shoots a line trace per candidate per query. It gets slower every time an artist adds
a crate, and slower again every time a designer adds an agent — and it gets slower in the middle of a
firefight, which is the one moment nobody has the budget.

Here, the sweeps that decide what a point *is* are paid **once**, in slices, under a millisecond ceiling
per frame. Afterwards a query is a spatial-hash lookup over a few cells and six multiply-adds per
candidate. **No trace per query.** It costs the same whether the level holds two hundred points or twenty
thousand — and the statistics box prints the microseconds per query *and* the candidate count next to it,
so the claim comes with its own evidence.

### The level does not freeze while it builds

Generation is sliced. At most *N* grid samples and *M* milliseconds per frame, with a live percentage and
the measured cost printed beside the configured budget. A level that stalls for two seconds while it
builds its cover is not a shipping feature, and treating that as part of the product rather than as a
detail is why the progress bar is on the box at all.

### One point, one agent — guaranteed

`Claim` and `Release`, exclusive. Send eight agents at once and they end up at **eight different points**.
Two never share one.

The claim happens *inside* the query, not a frame later from Blueprint. That is what makes it impossible
for two agents that decide on the same frame to be handed the same point — a helper function that returns
a point and leaves the caller to reserve it cannot make that promise, because between the two calls the
point is still free.

Every claim carries an expiry, because the agent that cannot release its point is precisely the one that
died holding it. Cover permanently owned by a corpse is a level that runs out of cover over an afternoon.

### Nothing is trusted forever

A door opens. A wall is blown out. A container is driven away. A point measured as safe is now a lie.

A small fixed budget of line traces per frame audits points that *claim* to block, sweeping the whole set
over seconds. One that turns out not to block is invalidated on the spot: it stops being returned, its
claim is freed, an event fires, and the counter on the box goes up. A background audit, not a per-query
check — the moment it costs enough to be worth optimising, it has stopped being free.

### What it is not

**It is not cover behaviour.** No Behavior Tree node. No animation. No montage. No agent that walks itself
into a point and leans out of it.

It answers **where** and **whose**. What an agent does when it arrives is your game's business, and it
always was — it depends on your animation graph, your weapon code, your damage model and your movement
component, none of which a cover *index* has any business knowing about. That separation is why this can
be dropped into a project on a Tuesday and be answering queries the same afternoon.

### Numbers you can read, in a Shipping build

A Canvas statistics box, thirteen lines:

- points generated, and the high/low split
- how many offer a way to lean out
- build progress, samples processed against total
- **milliseconds per frame, next to the configured budget**
- wall-clock milliseconds for the last whole build
- queries per second
- **microseconds per query**, averaged over the last second
- candidates scored, against how many a linear search would have touched
- points claimed, and claims expired
- spot-check traces fired, and points invalidated by them
- where the current threat is

Canvas, not a debug widget, so it survives a cooked Shipping build. Console commands `Cover.Build`,
`Cover.Stats`, `Cover.Show`, `Cover.Threat` and `Cover.Clear`.

---

## Features

- Automatic cover point generation from level geometry — eight sweeps per grid sample, no authoring
- Cover surface normal stored per point: one dot product is the whole definition of "is this cover"
- High cover against low cover, **measured** with a second sweep at standing height
- `PeekLeft` / `PeekRight` / `PeekOver` measured with reach-and-see sweeps, not guessed from a box
- Sliced generation under a millisecond-per-frame budget, with live progress
- Coarse spatial hash — queries fire **no traces** and do not scale with level size
- Six-term scoring: shielding angle, agent distance, threat distance, high/low, peek side, wall proximity
- `PreferredThreatDistance` turns the same cover field into a shotgun layout or a rifle layout
- Exclusive `Claim` / `Release` with expiry, claimed inside the query so two agents can never collide
- Throttled verification traces that invalidate cover the level no longer provides
- Partial rebuilds — regenerate one room without losing the rest of the level's points or claims
- Full Blueprint library, safe in worlds where the plugin is not set up at all
- Canvas statistics box that survives a Shipping build
- Five console commands
- Optional navmesh projection so no point is generated where an agent cannot stand
- Debug drawing: points coloured by shielding against the current threat, cover normals, peek markers

---

## Technical details

**Modules:** one runtime module, `LoadingPhase: PreDefault`.

**Dependencies:** `Core`, `CoreUObject`, `Engine`, `DeveloperSettings`, `NavigationSystem`;
`RenderCore` privately. No UMG in the product, no Niagara, no Chaos, no editor module, no network
replication.

**Classes:** `ACoverVolume`, `UCoverPointsSubsystem` (world subsystem, Game and PIE),
`UCoverPointsStatics` (Blueprint library), `UCoverPointsSettings` (Project Settings → Plugins),
`ACoverPointsHUD`, and the `FCoverPoint`, `FCoverPointHandle`, `FCoverQueryParams`, `FCoverQueryResult`
and `FCoverPointsStats` structs.

**Number of Blueprints:** 0 in the product. Demo content only.
**Number of C++ classes:** 5 classes, 5 structs, 2 enums.
**Network replicated:** No.
**Supported platforms:** Win64 built and verified. Mac and Linux enabled in the descriptor, not built for
this release.
**Supported engine versions:** 5.8

**Documentation:** `Docs/DOCUMENTATION.md` in the plugin folder.

---

## Screenshot captions

1. **The whole product in one frame.** A cover field, coloured live against a moving threat — green where
   the cover faces it, red where it turns its back on it. Recomputed every frame from one dot product per
   point.
2. **Generation in slices.** The progress bar mid-build, with the milliseconds spent this frame printed
   next to the configured budget. The level is still running.
3. **Eight agents, eight points.** `Claimed 8` on the box, and not two of them at the same wall.
4. **High, low and the ways out.** Peek markers drawn per point: left, right, over the top — measured
   against the actual geometry.
5. **Microseconds, not milliseconds.** The query counters, with the candidate count beside them: the
   evidence that the spatial index is doing the work the number claims.

---

© 2026 Simulated Flow. All rights reserved.
