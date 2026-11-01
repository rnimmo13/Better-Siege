# MASTERPLAN

Reference design brief for Better Siege. Use this document as the north star for future game changes: new systems, operators, maps and balance decisions should move the game toward the experience described here.

> Original request: *can you start working on a game similar to this description*

## Build status

| Milestone | Scope | Status |
| --- | --- | --- |
| 1. Core round loop | Operator select, preparation/action/post-plant phases, bomb sites with plant & disable, all win conditions, side swap and match end; hitscan guns with lethal headshots, falloff and wall penetration; 100/110/125 health; lean, crouch, sprint, vault; destructible walls, floors and barricades; reinforcement; soft & hard breach charges, shock box, shield, alarm sensor, pulse scan; AI bots for both teams; canvas HUD; greybox Embassy map | Done |
| 2. Information | Attacker recon drones (2 each, auto-deployed in preparation), defender security cameras, manual and passive spotting, eliminated players watch drones/cameras, bots drone the target site and shoot enemy drones/cameras, 45s attacker preparation | Done |
| 2.1 Aim & drone routes | Aim down sights on RMB (zoom, tighter spread, less recoil, slower movement and look); mouse wheel weapon swap; floor-level drone holes in walls, 25cm drone gap under door barricades, three hidden drone-only tunnels (Storm Drain into site B, Lounge Vent, Kitchen Duct); drone headlight | Done |
| 3. Survival, vertical play, breaching | Down-but-not-out (body-shot kills down you if a teammate is standing; 25 HP bleeding out, crawl, revive with F in 4s), bots revive teammates and hold fire on downed enemies; second floor over the whole north wing (Upstairs Office, Library, Archive) with destructible floors, structural beams left under blown floors, three hatches (two reinforceable site hatches), interior hallway staircase; attacker bots take breacher jobs — Ram opens soft site walls with breach charges, Torch burns reinforced site walls with thermal charges | Done |
| 4. Sound, rappelling, true ADS, team identity | Synthesized gunfire, footsteps (sprint loud, crouch quiet), explosions, thermal hiss, debris, gadget and device beeps, rope sounds; sound reaches you through doors, holes and stairwells via navigation paths plus a muffled through-wall layer, so new holes change what you hear; bots turn toward enemy noise; attackers rappel on three north-facade ropes (T to attach, W/S climb, A/D slide, Space crashes through a barricade, auto-face the wall), tall upstairs windows to enter through; true aim down sights — the arms and gun slide so the weapon's own sight sits on the view axis, crosshair fades out; blue vs red team gear (tinted vest, cap and shirt) and the model's holster pistol hidden | Done |
| 5. Main menu | Front-end map that the game boots into: Play (starts a match on the Embassy), Settings and Quit; Settings has Gameplay (mouse and ADS sensitivity, invert look, field of view, bot difficulty Recruit/Regular/Veteran), Audio (master volume) and Video (window mode, resolution, graphics quality, VSync, frame-rate limit) tabs with Apply, Back and Defaults; settings save to GameUserSettings.ini and apply in game | Done |
| 6. Basement and Embassy art pass | Basement under the south half: Vault, Cellar and Generator Room off a corridor, reached by stairs from the main hallway, outside cellar steps in a trench by the entrance, and destructible kitchen, lobby and storage floors with two hatches; each round the bomb sites are either the ground floor pair (A Office / B Server) or the basement pair (A Vault / B Generator); procedural tileable textures with normal maps (brick, plaster, wood, marble tile, concrete, carpet, pavers, brushed metal) on one world-aligned triplanar material, painted room colors, flat ceilings under floors, reinforced walls in steel; furniture and dressing in every room (kitchen counters, reception desk, server racks, sofas, desks, bookshelves, crates, vault door and deposit boxes, generator, boiler, pipes), ceiling lights with shadows, roofs over the whole building, flagpole, planters and trees outside | Done |
| 7. Firing range and pause menu | Lvl_Range sandbox opened from the main menu: shooting lanes at 10/25/50 m with fixed-health and moving targets, a wall row (soft, reinforced, barricade), a two-storey breach house (barricades, destructible floor, hatch, rope, drone duct, cameras, bomb site), allies to down and revive, a vault and lean course; switch to or restock any operator with 1-6, damage readout on the HUD, targets respawn, F6 resets; Esc pause menu in matches and the range with live settings | Done |
| Later | Menu-style HUD, second map, more operators, online multiplayer | Planned |

---

Rainbow Six Siege is a tactical first-person shooter built around five attackers trying to overcome five defenders inside a destructible environment. You choose specialized characters called operators, gather information, reshape the building, and fight over an objective in short rounds where death usually lasts until the next round.

What makes Siege distinctive is how its systems interact. A wall can be cover, an entry route, a surface for a defensive gadget, or something you shoot through. A drone can reveal an enemy, but it can also tell you which wall to open or which room you can safely enter. Winning involves understanding how those pieces connect.

I'll focus on the traditional 5v5 Bomb format, which best explains Siege's core competitive gameplay. Other playlists change some rules, and individual operator details change with updates.

## The basic objective and round structure

In Bomb, defenders protect two bomb sites inside a building. Attackers carry a device called the defuser, which they must plant in a valid area at either site and protect until its countdown finishes.

The main win conditions are:

| Attackers win by… | Defenders win by… |
| --- | --- |
| Eliminating all defenders. | Eliminating all attackers before a successful plant. |
| Planting the defuser and keeping it active until its countdown finishes. | Preventing a plant until the round timer expires. |
| | Disabling an activated defuser. |

Planting changes the entire round. Before the plant, attackers are under time pressure. After the plant, defenders must move to the device and disable it.

If attackers plant and then all die, defenders still have to disable the defuser. Killing the final attacker does not automatically end that situation.

A match consists of multiple rounds, with teams switching attacking and defending sides. Round counts, overtime, and some timing rules depend on the playlist.

## What happens before the fighting starts

Players select an operator and loadout. Your operator determines your unique ability, available weapons, secondary gadgets, and movement and health characteristics.

Traditional rounds begin with a preparation phase:

- Defenders reinforce surfaces, place gadgets, create openings, and establish positions.
- Attackers drive small reconnaissance drones through the building to locate the objective and examine the defense.
- Attackers can use that information to adjust their selection where attacker repick is available.

A common competitive ruleset uses 45 seconds of preparation and three minutes of action, although playlist settings can differ. These timings also appear in Ubisoft's published competitive rules. Ubisoft competitive rulebook

Preparation has lasting consequences. A badly reinforced wall can prevent your own team from moving. A well-hidden drone can provide information for most of the round.

## The action phase: how an attack develops

Attackers generally begin outside and must work their way toward the objective. Defenders already occupy the building.

A coordinated attack usually involves several overlapping jobs:

1. Find a safe entry. Check the room and nearby threats with a drone.
2. Gain map control. Take rooms, stairs, or floors needed to pressure the objective.
3. Deal with defenders away from site. Remove them, force them back, or contain them.
4. Clear defensive gadgets. Remove whatever prevents breaching or entering.
5. Open useful angles and routes. Breach walls, floors, or hatches.
6. Execute the objective push. Coordinate entry, cover, and the plant.
7. Protect the defuser. Establish positions that make disabling it difficult.

Those are strategic stages, not separate phases enforced by the game. Teams can rush, skip steps, or exploit an undefended site. The important question is whether they have created a workable path to winning.

## Gunplay: extremely lethal, with a heavy emphasis on the first accurate shot

Siege's gunfights can end almost immediately.

A defining rule is that a clean headshot from most conventional firearms is lethal, regardless of an operator's normal health pool. Shotgun pellets and particular weapon or gadget interactions have exceptions, so "every weapon always kills with one headshot" is too broad.

Body-shot damage depends on factors such as:

- The weapon.
- Distance and damage falloff.
- The body part hit.
- Penetration through surfaces.
- The target's remaining health.

Operators ordinarily have base health values of 100, 110, or 125, associated with their health rating; certain abilities can modify survivability. Ubisoft replaced the older hidden armor reduction system with explicit health pools. Ubisoft's health-system explanation

The practical consequences are substantial:

- Keeping your crosshair at likely head height reduces how much you must adjust.
- Knowing where someone is can matter more than reacting quickly after seeing them.
- Exposing yourself to several angles can get you killed before you identify the shooter.
- More health can help against body shots, but it does not make careless exposure safe.

Weapons have recoil, different rates of fire, magazine sizes, reload times, and aiming characteristics. Attachments change handling or visibility, with availability depending on the weapon and operator.

There is no traditional round-by-round weapon-buying economy. You enter with your selected loadout. Your important resources are your life, ammunition, gadgets, information, positioning, and time.

## Movement and peeking

Movement is deliberate because moving changes your readiness, noise, and exposure.

You can walk, sprint, crouch, go prone, lean, and vault suitable obstacles. Traversal uses contextual vaulting and climbing rather than a general-purpose jump.

Leaning lets you shift your viewpoint around cover while keeping more of your body protected. It helps you inspect an angle, but it does not make you invisible or invulnerable.

Sprinting lets you move quickly, but creates noise and requires a transition before accurate aimed fire. Crouching and slower movement can reduce your profile or noise, but they also make you slower to reposition.

A peek is exposing yourself to check or fight an angle. A swing usually means moving out decisively to challenge someone.

Good peeking involves:

- Checking one likely position at a time.
- Avoiding exposure to several enemies simultaneously.
- Using fresh information to aim at the right place.
- Changing your position after the enemy learns where you are.

Repeatedly peeking the same spot is dangerous because the opponent can already have their crosshair waiting there.

Attackers also use rappelling to reach roofs and windows and attack from outside elevations. It creates valuable angles, but leaves them exposed to certain windows, runouts, and nearby threats.

## Destruction: the mechanic that changes everything else

Siege's buildings have a mixture of destructible and indestructible surfaces. Learning which is which is fundamental.

| Surface | Gameplay significance |
| --- | --- |
| Soft walls | Can be damaged or opened with suitable weapons, melee, explosives, or breaching tools. |
| Reinforced walls | Resist ordinary gunfire and soft-breaching tools; usually require specialized hard-breaching equipment. |
| Destructible floors and ceilings | Allow attacks between floors, although structural beams often remain. |
| Hatches | Can provide vertical sightlines or routes when opened; some can be reinforced. |
| Door and window barricades | Obstruct access and visibility but can be broken. |
| Solid structural surfaces | Remain reliable barriers because they cannot be breached normally. |

Destruction is functional rather than purely visual.

You can make:

- A small hole to watch a doorway.
- A low opening to see someone's feet.
- A passage between rooms.
- A large breach that exposes an entire defensive position.
- A floor opening to attack equipment below.

An opening generally works in both directions. Creating a sightline toward the enemy can also expose you or a teammate.

Defenders therefore do not simply reinforce every available wall. They preserve some surfaces for deliberate openings and reinforce others to deny attackers easy access.

## Soft breaching, hard breaching, and their counters

A soft breacher opens ordinary destructible surfaces. A hard breacher can open reinforced surfaces.

That distinction creates a major layer of teamwork.

For example:

1. Defenders reinforce an important exterior wall.
2. A defender protects it with an electrification gadget.
3. An attacker brings a hard-breaching charge.
4. A teammate disables or destroys the defensive gadget.
5. The hard breacher opens the wall.
6. Other attackers cover the new opening and pressure the defenders.

Thermite is a classic example: his charges open large breaches, but he often needs teammates to remove counters and cover him while he works. Ubisoft's operator guide explicitly describes this dependency. Ubisoft's Thermite guide

The defender can also react while the breach is being attempted. Bandit tricking, for example, involves timing electrification to destroy breaching equipment. Attackers may respond by applying pressure from another direction.

The interaction is about placement and timing as much as bringing the right counter.

## Vertical gameplay

Rooms above and below the objective often matter as much as the objective itself.

Suppose a defender is safely holding a doorway behind cover. Attackers might struggle to beat that position from the hallway. Instead, they take the floor above and destroy the floor over the defender.

Now the defender must move, look upward, or risk being shot from above.

Vertical pressure can:

- Dislodge entrenched defenders.
- Destroy equipment hidden behind a wall.
- Threaten common planting positions.
- Force someone into a teammate's line of fire.
- Protect or deny a defuser interaction from another floor.

Structural beams often remain after a floor is opened. This creates narrow shooting channels rather than removing the whole floor.

Effective vertical play requires knowing how rooms line up across floors, not just recognizing the room you currently occupy.

## Information: drones, cameras, pings, and communication

Information is one of Siege's most valuable resources.

Attackers ordinarily have reconnaissance drones. Defenders have map cameras and can bring operators with additional observation tools.

These devices can tell you:

- Where an enemy is.
- Whether a room appears clear.
- Which gadgets protect an entrance.
- Whether someone has moved away from a position.
- Which route an opponent is using.

A drone helps convert an uncertain entry into a calculated one. A teammate can drive it ahead while another attacker follows close enough to act on the information.

But information expires. A room checked earlier may no longer be safe. An enemy can move as soon as the drone leaves.

Pings and verbal callouts communicate that information. Direct detection scans can warn opponents that they have been spotted, potentially exposing the observation tool. A location ping marks a place without inherently proving who occupies it.

Useful communication is specific:

> "One defender behind the desk in the upstairs office, watching the hallway."

That gives a teammate a location and an expected threat direction.

In the traditional no-respawn format, eliminated teammates can still help by watching available observation tools and communicating. A dead player who supplies timely information can meaningfully affect the round.

## Sound as a source of information

Footsteps, shots, barricade destruction, rappelling, gadget deployment, and other actions give opponents clues.

Siege's sound system considers paths through the environment. Sound can reach you through a doorway or opening, which means its apparent direction does not always point directly through the wall toward its source. Destruction changes those paths. Ubisoft's audio director explained this approach in a technical discussion of the system. Siege audio design explanation

This makes sound useful but imperfect.

You might hear someone nearby without knowing which floor they occupy. You might hear a breach begin and recognize that a particular wall is under threat. Loud destruction can also make quieter movements harder to notice.

Strong players combine sound with cameras, map knowledge, and teammate reports.

## Operators and team composition

An operator is a package of weapons, equipment, and a specialized ability. Your selection affects what problems the team can solve.

Common attacking functions include:

- Entry: takes early contested space.
- Hard breach: opens reinforced surfaces.
- Support: helps enable breaching, carries the defuser, or supplies utility.
- Soft breach: opens ordinary walls and floors.
- Flank watch: protects against defenders approaching from behind.
- Information gathering: locates enemies or defensive equipment.

Common defensive functions include:

- Anchor: holds positions on or near the objective.
- Roamer: operates farther from the objective to disrupt attackers.
- Breach denial: helps keep important reinforced surfaces closed.
- Area or plant denial: makes entering or planting dangerous.
- Information gathering: watches routes and identifies threats.
- Projectile protection: intercepts or neutralizes certain incoming equipment.

These are flexible responsibilities. An operator's gadget may suit a role, but the player still adapts to the round.

Team composition matters because five strong weapons do not necessarily give you a way through a protected wall, a means of gathering information, or enough tools to clear a fortified position.

## Gadgets and the utility exchange

Players often call usable equipment utility: drones, explosives, breaching tools, shields, traps, cameras, smoke, and similar resources.

Gadgets interact rather than functioning independently.

A defensive projectile interceptor may protect a shield. The shield protects a defender. That defender protects the doorway attackers need to cross.

Attackers must work out how to dismantle that arrangement. They might destroy the interceptor, use another route, attack vertically, or force the defender away.

This is often called a utility exchange: spending your equipment to remove the opponent's equipment or position.

The deeper question is whether the exchange creates progress. Using your final explosive to remove an unimportant gadget may leave you unable to deal with the position actually stopping your push.

## How defense works: time and space

Defenders benefit from attackers having a deadline.

They can win valuable time by:

- Keeping important walls closed.
- Destroying drones.
- Making attackers clear several rooms.
- Threatening a flank.
- Temporarily denying an entrance.
- Forcing attackers to spend equipment.

A roamer does not need several kills to succeed. If they make three attackers spend much of the round locating and clearing them, they may have done their job.

However, roaming also creates risk. A defender who stays too far away may be unable to return before the site falls.

Good defense balances disruption with the ability to support the objective.

## Crossfires, trades, and map control

A crossfire means teammates cover a location from different directions. An attacker who turns to fight one defender becomes exposed to the other.

A trade happens when a teammate quickly eliminates the opponent who just killed you. Positioning close enough to trade makes an attack more resilient.

Map control means your team can use an area with reasonable confidence while restricting the enemy's use of it. Simply walking through a room once does not establish lasting control.

These concepts explain why a coordinated pair can be more effective than two isolated players with excellent aim.

At the same time, stacking too closely invites explosives and makes it harder to cover different angles. Good spacing supports teammates while preserving room to move.

## Health, downing, and survival

Damage persists unless an applicable ability restores health. You do not normally regenerate to full health by waiting behind cover.

Some damage leaves an operator down but not out, or DBNO. The player is incapacitated and may be revived if the situation and available abilities permit it. Other damage kills outright.

Reviving creates a tactical decision: the rescuer becomes vulnerable, and the downed player may be watched by an enemy.

Friendly fire is also relevant in the core tactical format. Shooting or using explosives near teammates requires care.

Losing a player costs more than a weapon. It can remove a needed gadget, a route watcher, or the person carrying the defuser. This is why an apparently even kill trade can still favor one team strategically.

## What a coordinated round looks like

Imagine the defenders occupy a bomb site with a reinforced exterior wall and a destructible floor above it.

- During preparation, they reinforce the exterior wall, protect it with electrification, create a passage between the bomb rooms, and send a roamer upstairs.
- Attackers identify the setup with drones.
- Two attackers pressure upstairs while others prepare outside.
- The roamer delays them, then falls back rather than taking a hopeless fight.
- Attackers gain upstairs control and open the floor.
- Vertical pressure exposes defensive equipment and forces an anchor to move.
- The outside group removes wall protection and opens the reinforced wall.
- One attacker watches the flank while teammates cover the plant.
- The defuser is activated.
- Surviving defenders must retake the area and find enough time to disable it.

Every stage changes the next one. Clearing upstairs enables vertical pressure. Vertical pressure enables the breach. The breach enables the plant. The plant forces defenders out of their established positions.

## What mastery actually involves

Siege rewards several abilities at once: accurate shooting, detailed map knowledge, recognition of gadget interactions, sound interpretation, communication, and judgment under time pressure.

A strong player constantly asks:

- What do we know, and how recent is that information?
- Which position prevents us from progressing?
- What equipment can solve that problem?
- Who can cover or trade the next player moving?
- How much time remains?
- Does this fight help us win the objective?

The most characteristic Siege play is a sequence where information reveals a problem, destruction creates an opportunity, teammates coordinate around it, and a brief gunfight decides whether the plan succeeds.
