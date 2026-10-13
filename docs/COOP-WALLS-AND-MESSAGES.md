# Co-op walls and co-op messages

Generated 2026-09-25 from the game files by `tools/gamedata` (fog_survey.py, wall_scripts.py,
chalice_walls.py, gen_messages.py, gen_docs.py). Nothing here was typed by hand.

## Co-op fog walls (removed)

A co-op wall is any wall switched by event script **7600** (common.emevd) or **12907600**
(m29.emevd). BBSC refuses the draw / collision / fog-on calls those two scripts make for their
own wall, so every such wall stays down in every map. Nothing else is touched: boss walls are
switched by boss events and are never affected. The scripts only raise walls in multiplayer,
so single player is unchanged.

### Base game and DLC

| Area | Wall object | Fog SFX | Name in the map file |
|---|---|---|---|
| Hemwick Charnel Lane | 2201999 | 2203999 | SFX_霧壁_マルチ_XX |
| Old Yharnam | 2301999 | 2303999 | SFX_霧壁_マルチ_聖堂街Aとの接続 |
| Cathedral Ward | 2401995 | 2403995 | SFX_霧壁_マルチ_墓地街 |
| Cathedral Ward | 2401996 | 2403996 | SFX_霧壁_マルチ_生贄 |
| Cathedral Ward | 2401997 | 2403997 | SFX_霧壁_マルチ_廃墟 |
| Cathedral Ward | 2401998 | 2403998 | SFX_霧壁_マルチ_聖堂街B（大橋 |
| Cathedral Ward | 2401999 | 2403999 | SFX_霧壁_マルチ_聖堂街B（避難所 |
| Central Yharnam | 2411997 | 2413997 | SFX_霧壁_マルチ_ガスコイン部屋出口 |
| Central Yharnam | 2411998 | 2413998 | (no fog SFX placed in the map) |
| Central Yharnam | 2411999 | 2413999 | SFX_霧壁_マルチ_診療所 |
| Upper Cathedral Ward | 2421998 | 2423998 | Sfx_霧壁_マルチ_落とし子エリアとの接続 |
| Upper Cathedral Ward | 2421999 | 2423999 | Sfx_霧壁_マルチ_聖堂街Aとの接続 |
| Forbidden Woods | 2701998 | 2703998 | SFX_霧壁_マルチ_聖堂街Bとの接続 |
| Forbidden Woods | 2701999 | 2703999 | SFX_霧壁_マルチ_聖堂街Aとの接続 |
| Yahar'gul | 2801998 | 2803998 | SFX_光の壁_マルチ1 |
| Yahar'gul | 2801999 | 2803999 | SFX_光の壁_マルチ0 |
| Byrgenwerth | 3201999 | 3203999 | SFX_霧壁_マルチ_森との接続 |
| Research Hall / Clocktower (DLC) | 3501990 | 3503990 | SFX_霧壁_マルチ閉じ込め_聖堂街D側 |

### Chalice dungeons

8397 co-op wall placements (named マルチ閉じ込め壁) across 2566 blocks, all switched by 12907600.
Their entity ids are reused between blocks - 79 of the 88 Chalice co-op fog ids are boss or door
walls in other blocks - which is why BBSC recognises them by the script, never by id.

### Boss walls (kept)

Fog SFX in the xx03800-xx03899 range, switched by the boss events: for example Gascoigne
2413810, Vicar Amelia 2413800, Rom 3203800, Ludwig 3403800, Lady Maria 3503800, Orphan
3603800, and in the Chalices Sfx_Boss壁 2902690 / 2902691.

## Co-op messages (replaced)

English text in the event text file (category 30). Messages about really leaving - Silencing
Blank, lost connection, adversaries - are unchanged. `/` marks a line break.

| Id | Game text | BBSC text |
|---|---|---|
| 102001 | The bell's resonance has beckoned <?joinName?>. | <?joinName?> has joined the hunt. / The hunters' journey begins. |
| 102003 | <?joinName?> has arrived by the bell's resonance | <?joinName?> has joined the hunt. |
| 102004 | <?joinName?> has arrived by the bell's resonance | <?joinName?> has joined the hunt. |
| 102005 | <?joinName?> has arrived by the bell's resonance | <?joinName?> has joined the hunt. |
| 102006 | Confederate <?joinName?> has arrived by the bell's resonance. | Confederate <?joinName?> has joined the hunt. |
| 102007 | Confederate <?joinName?> has arrived by the bell's resonance. | Confederate <?joinName?> has joined the hunt. |
| 102056 | Confederate <?joinName?> has arrived by the bell's resonance. | Confederate <?joinName?> has joined the hunt. |
| 102057 | Confederate <?joinName?> has arrived by the bell's resonance. | Confederate <?joinName?> has joined the hunt. |
| 102051 | Old Hunter <?joinName?> has arrived by the bell's resonance. | Old Hunter <?joinName?> has joined the hunt. |
| 102052 | Old Hunter <?joinName?> has arrived by the bell's resonance. | Old Hunter <?joinName?> has joined the hunt. |
| 102053 | Old Hunter <?joinName?> has arrived by the bell's resonance. | Old Hunter <?joinName?> has joined the hunt. |
| 102054 | Old Hunter <?joinName?> has arrived by the bell's resonance. | Old Hunter <?joinName?> has joined the hunt. |
| 102055 | Old Hunter <?joinName?> has arrived by the bell's resonance. | Old Hunter <?joinName?> has joined the hunt. |
| 102100 | Entered the world of <?hostName?>, who beckoned you here. | The hunters' journey begins. / You now hunt beside <?hostName?>. |
| 102200 | Entered the world of <?hostName?>. | The hunters' journey begins. / You now hunt beside <?hostName?>. |
| 102600 | You are now in the world of the beckoner, <?hostName?>. | The hunters' journey begins. / You now hunt beside <?hostName?>. |
| 102700 | You are now in the world of the beckoner, <?hostName?>. | The hunters' journey begins. / You now hunt beside <?hostName?>. |
| 103100 | You died. Returning to your world. | You died. |
| 103200 | You died. Returning to your world. | You died. |
| 103300 | You died. Returning to your world. | You died. |
| 103400 | You died. Returning to your world. | You died. |
| 103500 | You died. Returning to your world. | You died. |
| 103101 | Your beckoner has died. Returning to own world. | Your beckoner has died. / The hunt goes on. |
| 103001 | Cooperator <?deadName?> has died. | Hunter <?deadName?> has died. / They will rise again. |
| 103006 | Confederate <?deadName?> has died. | Confederate <?deadName?> has died. / They will rise again. |
| 103007 | Confederate <?deadName?> has died. | Confederate <?deadName?> has died. / They will rise again. |
| 103056 | Confederate <?deadName?> has died. | Confederate <?deadName?> has died. / They will rise again. |
| 103057 | Confederate <?deadName?> has died. | Confederate <?deadName?> has died. / They will rise again. |
| 103051 | Old Hunter <?deadName?> has died. | Old Hunter <?deadName?> has died. / They will rise again. |
| 103052 | Old Hunter <?deadName?> has died. | Old Hunter <?deadName?> has died. / They will rise again. |
| 103053 | Old Hunter <?deadName?> has died. | Old Hunter <?deadName?> has died. / They will rise again. |
| 103054 | Old Hunter <?deadName?> has died. | Old Hunter <?deadName?> has died. / They will rise again. |
| 103055 | Old Hunter <?deadName?> has died. | Old Hunter <?deadName?> has died. / They will rise again. |
| 106200 | Beckoner <?hostName?> has fallen. It is time to bid farewell. | Beckoner <?hostName?> has fallen. / The hunt goes on. |
| 106100 | <?npcNameId@1000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@1000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142010 | <?npcNameId@1010?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@1010?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142020 | <?npcNameId@106000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@106000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142030 | <?npcNameId@127000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@127000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142040 | <?npcNameId@209000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@209000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142050 | <?npcNameId@210000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@210000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142060 | <?npcNameId@212010?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@212010?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142070 | <?npcNameId@212020?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@212020?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142080 | <?npcNameId@212030?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@212030?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142090 | <?npcNameId@216000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@216000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142100 | <?npcNameId@232000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@232000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142110 | <?npcNameId@251000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@251000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142120 | <?npcNameId@257000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@257000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142130 | <?npcNameId@271000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@271000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142140 | <?npcNameId@272000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@272000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142150 | <?npcNameId@304000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@304000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142160 | <?npcNameId@305000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@305000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142170 | <?npcNameId@306000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@306000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142180 | <?npcNameId@313000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@313000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142190 | <?npcNameId@500000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@500000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142200 | <?npcNameId@501000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@501000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142210 | <?npcNameId@502000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@502000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142220 | <?npcNameId@503000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@503000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142230 | <?npcNameId@504000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@504000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142240 | <?npcNameId@505000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@505000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142250 | <?npcNameId@506000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@506000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142260 | <?npcNameId@507000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@507000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142270 | <?npcNameId@508000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@508000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142280 | <?npcNameId@509000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@509000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142290 | <?npcNameId@510000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@510000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142300 | <?npcNameId@511000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@511000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142310 | <?npcNameId@512000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@512000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142320 | <?npcNameId@513000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@513000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142330 | <?npcNameId@540000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@540000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142340 | <?npcNameId@551000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@551000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142350 | <?npcNameId@750000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@750000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142360 | <?npcNameId@804000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@804000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142370 | <?npcNameId@805000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@805000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 142380 | <?npcNameId@899000?> was defeated, ending the hunt. Now, say farewell. | <?npcNameId@899000?> was slain. Area cleared. / Prey slaughtered - the hunt goes on. |
| 102002 | Adversary <?joinName?> has arrived. | An enemy hunter, <?joinName?>, has invaded! / Be prepared. |
| 109000 | Drawn by the bell's timbre, a bell-ringing woman has appeared somewhere. | A bell-ringing woman has appeared somewhere. / Beware - an enemy hunter may come. |
| 103002 | Adversary <?deadName?> has died. | The invader <?deadName?> has been slain. |
| 103003 | Vileblood <?deadName?> has died. | Vileblood <?deadName?> has been slain. |
| 103004 | Executioner <?deadName?> has died. | Executioner <?deadName?> has been slain. |
| 103005 | Hunter of Hunters, <?deadName?>, has died. | Hunter of Hunters, <?deadName?>, has been slain. |
| 20000407 | Adversary <?leaveName?> returned to their own world. | The invader <?leaveName?> has fled / to their own world. |
| 20000408 | Adversary <?leaveName?> returned to their own world. | The invader <?leaveName?> has fled / to their own world. |
| 20000409 | Vileblood <?leaveName?> returned to their own world. | Vileblood <?leaveName?> has fled / to their own world. |
| 20000410 | Vileblood <?leaveName?> returned to their own world. | Vileblood <?leaveName?> has fled / to their own world. |
| 20000411 | Executioner <?leaveName?> returned to their own world. | Executioner <?leaveName?> has fled / to their own world. |
| 20000412 | Executioner <?leaveName?> returned to their own world. | Executioner <?leaveName?> has fled / to their own world. |
| 20000413 | Hunter of Hunters <?leaveName?> returned to their own world. | Hunter of Hunters <?leaveName?> has fled / to their own world. |
| 20000414 | Hunter of Hunters <?leaveName?> returned to their own world. | Hunter of Hunters <?leaveName?> has fled / to their own world. |
| 102300 | You are now in the world of the beckoner, Executioner <?hostName?> | You have entered the world of Executioner <?hostName?> / as their adversary. |
| 102400 | You are now in the world of the beckoner, Vileblood <?hostName?> | You have entered the world of Vileblood <?hostName?> / as their adversary. |
| 102500 | You are now in the world of the beckoner, the blood-addled hunter <?hostName?>. | You have entered the world of the blood-addled hunter <?hostName?> / as their adversary. |
| 101001 | Discovered the chime of a Small Resonant Bell. | A hunter has answered your bell. |
| 101003 | Discovered the chime of a Small Resonant Bell. | A hunter has answered your bell. |
| 101004 | Discovered the chime of a Small Resonant Bell. | A hunter has answered your bell. |
| 101005 | Discovered the chime of a Small Resonant Bell. | A hunter has answered your bell. |
| 101100 | Discovered the chime of a Beckoning Bell. | A Beckoning Bell answers. / The hunt awaits. |
