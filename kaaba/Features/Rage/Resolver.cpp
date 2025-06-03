#include "Resolver.hpp"
#include "../Visuals/EventLogger.hpp"
#include "../Scripting/Scripting.hpp"
#include <sstream>
#include "Autowall.hpp"
#include "AntiAim.hpp"
#include <iomanip>
#include "../Miscellaneous/PlayerList.hpp"
#include "../../SDK/Classes/IEngineTrace.hpp" // Corrected relative path

Resolver g_Resolver;

void Resolver::UpdateDesyncDetection( AnimationRecord *pRecord, AnimationRecord *pPreviousRecord, AnimationRecord *pPenultimateRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );
	if( !pRecord )
		return resolverData.ResetDesyncData( );

	if( !pPreviousRecord )
		return resolverData.ResetDesyncData( );

	if( !pPenultimateRecord )
		return resolverData.ResetDesyncData( );

	// player can only be on ground when doing this
	if( !( pRecord->m_fFlags bitand FL_ONGROUND ) )
		return resolverData.ResetDesyncData( );

	// log the playback rates
	resolverData.m_tLoggedPlaybackRates = std::make_tuple(
		pRecord->m_pServerAnimOverlays[ 6 ].m_flPlaybackRate * 1000.f,
		pPreviousRecord->m_pServerAnimOverlays[ 6 ].m_flPlaybackRate * 1000.f,
		pPenultimateRecord->m_pServerAnimOverlays[ 6 ].m_flPlaybackRate * 1000.f );

	const float flLatestMovePlayback = std::get<0>( resolverData.m_tLoggedPlaybackRates );
	const float flPreviousMovePlayback = std::get<1>( resolverData.m_tLoggedPlaybackRates );
	const float flPenultimateMovePlayback = std::get<2>( resolverData.m_tLoggedPlaybackRates );

#pragma region SPECIFIC_DETECT
	const bool bDetectedFlickOne = flLatestMovePlayback >= 1.f && flPreviousMovePlayback < 1.f && flPenultimateMovePlayback >= 1.f;
	const bool bDetectedFlickTwo = flLatestMovePlayback >= 1.f && flPreviousMovePlayback < 1.f && flPenultimateMovePlayback < 1.f;
	if( bDetectedFlickOne or bDetectedFlickTwo ) {
		// take note that we detected this
		++resolverData.m_nFlickDetectionCount;

		if (resolverData.m_nLastFlickTick != 0) { // If this is not the first flick detected since reset
			int current_interval = abs(g_pGlobalVars->tickcount - resolverData.m_nLastFlickTick);
			resolverData.m_nLastFlickDetectionFrequency = resolverData.m_nFlickDetectionFrequency; // Store the previous interval
			resolverData.m_nFlickDetectionFrequency = current_interval; // Set the new interval
		} else {image.png
			// This is the first flick since a reset or the first ever.
			// No interval can be calculated yet. Reset frequencies.
			resolverData.m_nLastFlickDetectionFrequency = 0;
			resolverData.m_nFlickDetectionFrequency = 0;
		}
		
		// take note of the tick we detected this thing in
		resolverData.m_nLastFlickTick = g_pGlobalVars->tickcount;
	}

	// Check if we should reset desync data due to inactivity
	if (resolverData.m_nFlickDetectionCount > 0 && resolverData.m_nLastFlickTick > 0) {
		const int DESYNC_RESET_THRESHOLD_TICKS = 10; // Was 14
		if (abs(resolverData.m_nLastFlickTick - g_pGlobalVars->tickcount) > DESYNC_RESET_THRESHOLD_TICKS) {
			resolverData.ResetDesyncData();
			// If data is reset, m_bDesyncFlicking and m_bConstantFrequency are false.
			// Exit here as further logic in this function call is not needed.
			return;
		}
	}

	// Determine m_bConstantFrequency and m_bDesyncFlicking
	// These flags are false if ResetDesyncData() was called above, or they hold values from the previous tick.
	// If conditions are no longer met, they should be set to false.

	const int MIN_FLICKS_FOR_CONSTANT_CHECK = 2; // Need at least 2 flicks to have one interval calculated
	const int MIN_FLICKS_FOR_DESYNC_CONFIRMATION = 3; // Need at least 3 flicks and constant frequency

	bool bIsFrequencyConstantThisFrame = false;
	if (resolverData.m_nFlickDetectionCount >= MIN_FLICKS_FOR_CONSTANT_CHECK) {
		// Check if the current and previous intervals are the same and non-zero
		if (resolverData.m_nFlickDetectionFrequency > 0 && resolverData.m_nFlickDetectionFrequency == resolverData.m_nLastFlickDetectionFrequency) {
			bIsFrequencyConstantThisFrame = true;
		}
	}
	resolverData.m_bConstantFrequency = bIsFrequencyConstantThisFrame;

	if (resolverData.m_bConstantFrequency && resolverData.m_nFlickDetectionCount >= MIN_FLICKS_FOR_DESYNC_CONFIRMATION) {
		resolverData.m_bDesyncFlicking = true;
	} else {
		resolverData.m_bDesyncFlicking = false;
	}

#pragma endregion

}

bool Resolver::IsAnglePlausible( AnimationRecord *pRecord, float flAngle, bool isNearWall, QAngle wallAngle ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );

	// LBY is generally the most reliable reference point, especially if updated recently.
	float flPredictedLBY = pRecord->m_flLowerBodyYawTarget;
	float flLBYDiff = Math::AngleDiff( flAngle, flPredictedLBY );

	// Tolerance for how far an angle can be from LBY to be considered plausible.
	// This might vary based on break type, or if they are desync flicking.
	float flLBYTolerance = XorFlt(65.f); // Default tolerance

	if (resolverData.m_eBodyBreakType == BREAK_LOW) {
		flLBYTolerance = XorFlt(80.f); // More tolerance for low breaks as they can be more varied
	} else if (resolverData.m_eBodyBreakType == BREAK_HIGH) {
		flLBYTolerance = XorFlt(50.f); // Less tolerance for high breaks, should be closer to LBY
	}

	if (resolverData.m_bDesyncFlicking) {
		flLBYTolerance = XorFlt(90.f); // Even more tolerance if actively desync flicking
	}

	// Check against LBY
	if (std::abs(flLBYDiff) < flLBYTolerance) {
		return true;
	}

	// Check against last moving LBY if available
	if (resolverData.m_flLastMoveBody != FLT_MAX) {
		float flLastMoveLBYDiff = Math::AngleDiff(flAngle, resolverData.m_flLastMoveBody);
		if (std::abs(flLastMoveLBYDiff) < XorFlt(60.f)) { // Slightly tighter tolerance for last move LBY
			return true;
		}
	}

	// Check against freestand angle if available
	if (resolverData.m_flFreestandYaw != FLT_MAX) {
		float flFreestandDiff = Math::AngleDiff(flAngle, resolverData.m_flFreestandYaw);
		if (std::abs(flFreestandDiff) < XorFlt(45.f)) { // Freestand should be relatively precise
			return true;
		}
	}

	// Check against edge-related angles if near a wall
	if (isNearWall) {
		// Angle perpendicular to the wall (facing away from wall)
		float flWallPerpendicular1 = Math::AngleNormalize(wallAngle.y + 90.f);
		float flWallPerpendicular2 = Math::AngleNormalize(wallAngle.y - 90.f);
		// Angle parallel to the wall (could be LBY flicking along wall)
		// float flWallParallel1 = wallAngle.y;
		// float flWallParallel2 = Math::AngleNormalize(wallAngle.y + 180.f);

		if (std::abs(Math::AngleDiff(flAngle, flWallPerpendicular1)) < XorFlt(50.f)) { // Tolerance for edge angles
			return true;
		}
		if (std::abs(Math::AngleDiff(flAngle, flWallPerpendicular2)) < XorFlt(50.f)) {
			return true;
		}

		// More specific check for LBY + wall offset
		// e.g., if their LBY is X, and wall is Y, they might flick to Y+Z or similar
		// This part can be expanded based on common edge anti-aim techniques
	}

	// If no other plausible conditions met, it's likely not a good angle.
	return false;
}

void Resolver::UpdateLBYPrediction( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );
	if( !pRecord )
		return;

	// don't do
	if( resolverData.m_iMissedShotsLBY >= 2 ) {
		pRecord->m_bLBYFlicked = false;
		resolverData.m_bInPredictionStage = false;
		return;
	}

	if( g_PlayerList.GetSettings( pRecord->m_pEntity->GetSteamID( ) ).m_bDisableBodyPred ) {
		resolverData.ResetPredData( );
		return;
	}

	// LBY prediction is primarily for on-ground, relatively static players.
	if( pRecord->m_vecVelocity.Length( ) > XorFlt( 0.1f ) || !( pRecord->m_fFlags & FL_ONGROUND ) ) {
		resolverData.m_flLowerBodyRealignTimer = FLT_MAX;
		pRecord->m_bLBYFlicked = false;
		resolverData.m_bInPredictionStage = false;
		return;
	}

	const float flXored1_1 = XorFlt( 1.1f );
	// If LBY has changed significantly enough to be considered a deliberate update attempt.
	// Threshold increased from XorFlt(1.f) to XorFlt(20.f) for robustness.
	if( resolverData.m_flPreviousLowerBodyYaw != FLT_MAX && fabs( Math::AngleDiff( pRecord->m_flLowerBodyYawTarget, resolverData.m_flPreviousLowerBodyYaw ) ) > XorFlt( 20.f ) ) {
		resolverData.m_flLowerBodyRealignTimer = pRecord->m_flAnimationTime + flXored1_1; // Predict next flick in ~1.1s
		pRecord->m_bLBYFlicked = true; // Mark that an initial LBY flick/change was detected
	}

	resolverData.m_flPreviousLowerBodyYaw = pRecord->m_flLowerBodyYawTarget;

	// If the predicted LBY realign time has come.
	if( pRecord->m_flAnimationTime >= resolverData.m_flLowerBodyRealignTimer && resolverData.m_flLowerBodyRealignTimer < FLT_MAX ) {
		resolverData.m_bInPredictionStage = true; // We are now in the LBY prediction window
		resolverData.m_flLowerBodyRealignTimer = pRecord->m_flAnimationTime + flXored1_1; // Predict the *next* one
		pRecord->m_bLBYFlicked = true; // Mark that a predicted LBY flick should be happening
	}
}

void Resolver::UpdateResolverStage( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );

	const float flXored01 = XorFlt( 0.1f );
	bool bInPredictionStage = pRecord->m_bLBYFlicked;

	// if desync flick is detected,
	// do NOT FORCE LBY!!! USE LOGIC INSTEAD!
	if( resolverData.m_bDesyncFlicking ) {
		resolverData.m_eCurrentStage = RESOLVE_MODE_LOGIC;
	}
	// WE GOOD
	else {
		if( ( pRecord->m_fFlags & FL_ONGROUND ) && ( pRecord->m_vecVelocity.Length( ) <= flXored01 || ( pRecord->m_bIsFakewalking ) ) )
			resolverData.m_eCurrentStage = EResolverStages::RESOLVE_MODE_STAND;

		if( ( pRecord->m_fFlags & FL_ONGROUND ) && ( pRecord->m_vecVelocity.Length( ) > flXored01 && !( pRecord->m_bIsFakewalking ) ) ) {
			resolverData.m_eCurrentStage = EResolverStages::RESOLVE_MODE_MOVE;
		}
		else if( !( pRecord->m_fFlags & FL_ONGROUND ) ) {
			resolverData.m_eCurrentStage = EResolverStages::RESOLVE_MODE_AIR;
		}
		else if( bInPredictionStage ) {
			resolverData.m_eCurrentStage = EResolverStages::RESOLVE_MODE_PRED;
		}
	}

	// allow lby flicks on override
	// also important allow override to override micro moving players because of the
	// falco kid who uses his shitty exploit =D
	if( g_Vars.rage.antiaim_resolver_override && g_Vars.rage.antiaim_correction_override.enabled && ( !bInPredictionStage || resolverData.m_bDesyncFlicking ) && pRecord->m_vecVelocity.Length( ) < XorFlt( 20.f ) ) {
		resolverData.m_eCurrentStage = EResolverStages::RESOLVE_MODE_OVERRIDE;
	}
}

void Resolver::ResolveBrute( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );
	auto pLocal = C_CSPlayer::GetLocalPlayer( );

	resolverData.m_iCurrentResolverType = 100; // Default Brute type

	float flLBY = pRecord->m_flLowerBodyYawTarget;
	float flLastMoveLBY = resolverData.m_flLastMoveBody;
	float flFreestand = resolverData.m_flFreestandYaw;
	float flEdge = resolverData.m_angEdgeAngle.y; // This is the general edge, might be different from wallAngle
	
	QAngle angAway = QAngle(0,0,0);
	if(pLocal && !pLocal->IsDead()) {
		Math::VectorAngles( pRecord->m_vecOrigin - pLocal->m_vecOrigin(), angAway );
	}

	std::vector<float> angles_to_try;
	angles_to_try.push_back(flLBY); 
	if (flFreestand != FLT_MAX) angles_to_try.push_back(flFreestand);
	else if (pLocal) angles_to_try.push_back(angAway.y + XorFlt(180.f)); // Fallback for freestand

	if (flEdge != 0.f) angles_to_try.push_back(flEdge);
	
	angles_to_try.push_back(Math::AngleNormalize(flLBY + XorFlt(110.f)));
	angles_to_try.push_back(Math::AngleNormalize(flLBY - XorFlt(110.f)));

	if (pLocal) {
		angles_to_try.push_back(Math::AngleNormalize(angAway.y + XorFlt(90.f)));
		angles_to_try.push_back(Math::AngleNormalize(angAway.y - XorFlt(90.f)));
	}

	if (flLastMoveLBY != FLT_MAX) {
		angles_to_try.push_back(flLastMoveLBY);
		angles_to_try.push_back(Math::AngleNormalize(flLBY + XorFlt(45.f))); // Original logic had LBY + 45
	}

	// Add specific wall-related angles if player is near a wall
	if (resolverData.m_bIsNearWall_ForBrute) {
		resolverData.m_iCurrentResolverType = 101; // Brute - Wall Specific
		float wallPerp1 = Math::AngleNormalize(resolverData.m_angWallAngle_ForBrute.y + 90.f);
		float wallPerp2 = Math::AngleNormalize(resolverData.m_angWallAngle_ForBrute.y - 90.f);
		angles_to_try.push_back(wallPerp1);
		angles_to_try.push_back(wallPerp2);

		// LBY aligned to wall perpendiculars
		float lbyDiffToWall1 = Math::AngleDiff(flLBY, wallPerp1);
		float lbyDiffToWall2 = Math::AngleDiff(flLBY, wallPerp2);
		if (std::abs(lbyDiffToWall1) < std::abs(lbyDiffToWall2)) {
			angles_to_try.push_back(wallPerp1); // LBY snapped to closest wall perp
		} else {
			angles_to_try.push_back(wallPerp2);
		}

		// LBY +/- smaller offsets when near wall
		angles_to_try.push_back(Math::AngleNormalize(flLBY + XorFlt(30.f)));
		angles_to_try.push_back(Math::AngleNormalize(flLBY - XorFlt(30.f)));
		angles_to_try.push_back(Math::AngleNormalize(flLBY + XorFlt(60.f)));
		angles_to_try.push_back(Math::AngleNormalize(flLBY - XorFlt(60.f)));
	}

	// Remove duplicates and ensure angles are somewhat unique before trying
	std::sort(angles_to_try.begin(), angles_to_try.end());
	angles_to_try.erase(std::unique(angles_to_try.begin(), angles_to_try.end(), [](float a, float b){ return std::abs(Math::AngleDiff(a,b)) < 5.f; }), angles_to_try.end());

	if (angles_to_try.empty()) { // Should not happen with current logic but as a safeguard
		pRecord->m_angEyeAngles.y = flLBY;
		return;
	}

	pRecord->m_angEyeAngles.y = angles_to_try[resolverData.m_iMissedShots % angles_to_try.size()];
}

void Resolver::UpdateBodyDetection( AnimationRecord *pRecord, AnimationRecord *pPreviousRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );

	if( !pRecord || !pPreviousRecord )
		return;

	C_AnimationLayer *pCurrentAdjustLayer = &pRecord->m_pServerAnimOverlays[ ANIMATION_LAYER_ADJUST ];
	C_AnimationLayer *pPreviousAdjustLayer = &pPreviousRecord->m_pServerAnimOverlays[ ANIMATION_LAYER_ADJUST ];

	if( !pCurrentAdjustLayer || !pPreviousAdjustLayer )
		return;

	// assume they're always breaking lby, incase this check fails
	if( !( resolverData.m_bBreakingLowerBody ) ) {
		resolverData.m_bBreakingLowerBody = true;

		// assume low breaker for now, it's the most common.
		resolverData.m_eBodyBreakType = EBodyBreakTypes::BREAK_LOW;
	}

	// reset this, it will get overwritten under the right
	// conditions anyway.
	resolverData.m_bHasntUpdated = false;

	if( resolverData.m_iMissedShotsLBY >= 2 || resolverData.m_iMissedShots >= 2 ) {
		resolverData.m_bBreakingLowerBody = true;
		return;
	}

	// see if the 979 animation was triggered
	if( pRecord->m_pEntity->GetSequenceActivity( pCurrentAdjustLayer->m_nSequence ) == ACT_CSGO_IDLE_TURN_BALANCEADJUST/* &&
		pRecord->m_pEntity->GetSequenceActivity( pPreviousAdjustLayer->m_nSequence ) == ACT_CSGO_IDLE_TURN_BALANCEADJUST*/ ) {
		// -- detect lby breakers above 120 delta
		if( !( pCurrentAdjustLayer->m_flWeight == 0.f && pCurrentAdjustLayer->m_flWeight == pPreviousAdjustLayer->m_flWeight ) ) {
			// balance adjust animation has started playing or is currently about to play again
			if( pCurrentAdjustLayer->m_flWeight == 1.f || pCurrentAdjustLayer->m_flWeight != pPreviousAdjustLayer->m_flWeight ) {
				resolverData.m_flLastWeightTriggerTime = pRecord->m_flAnimationTime;
			}

			// animation keeps playing or restarting, means that they are 
			// either moving their yaw left very fast or are breaking lby above 120 delta

			// since animation layers update every time the enemies choke cycle restarts, see if 
			// the last weight update time exceeds their choke time plus some tolerance (2, random number)
			if( TIME_TO_TICKS( fabs( resolverData.m_flLastWeightTriggerTime - pRecord->m_flAnimationTime ) ) < pRecord->m_iChokeTicks + 2 ) {
				// while weight is 1.f or incrementing from 0, the cycle increments until some number (~0.6)
				// once cycle reaches that number, both weight and cycle restart and go to zero once again.
				if( pCurrentAdjustLayer->m_flWeight == 1.f || pCurrentAdjustLayer->m_flWeight != pPreviousAdjustLayer->m_flWeight ) {
					if( pCurrentAdjustLayer->m_flCycle != pPreviousAdjustLayer->m_flCycle ) {
						resolverData.m_flLastCycleTriggerTime = pRecord->m_flAnimationTime;
					}

					// since animation layers update every time the enemies choke cycle restarts, see if 
					// the last cycle update time exceeds their choke time plus some tolerance
					if( TIME_TO_TICKS( fabs( resolverData.m_flLastCycleTriggerTime - pRecord->m_flAnimationTime ) )
						< pRecord->m_iChokeTicks + 2 ) {
						// cycle keeps changing, we can safely assume that this player is either
						// breaking lby over 120 delta or failing to break (to the left)
						resolverData.m_bBreakingLowerBody = true;
						resolverData.m_flLastUpdateTime = pRecord->m_flAnimationTime;

						// mark current breaker type as high delta.
						resolverData.m_eBodyBreakType = EBodyBreakTypes::BREAK_HIGH;
					}
				}
			}
			else {
				// not sure if this could even happen,
				// but i was changing some logic around and was weary
				// of this, so better be safe than sorry
				goto BREAKING_LOW_OR_NONE;
			}
		}
		// -- detect lby breakers under 120 delta
		else {
		BREAKING_LOW_OR_NONE:
			// the 979 animation hasn't played in a while. this can mean two things:
			// the enemy is no longer breaking lby, or the enemy is breaking lby but
			// supressing the 979 animation from restarting, meaning they're breaking under 120 delta.
			if( pCurrentAdjustLayer->m_flWeight == 0.f && pCurrentAdjustLayer->m_flCycle > 0.9f /*more like 0.96 but better be safe than sorry*/ ) {
				// so here's the problem. due to the nature of how lby breakers work, we can't really
				// detect an "lby update" (coz the whole point of LBY breakers is to keep your lby 
				// at one angle, and real at a different angle, as long as it's not in the same place).
				// so lby will always be the same. the check below will only be triggered when the player
				// fails to break lby, and when that happens only 2 things can happen, first one being
				// they trigger 979 and the first check owns them, second being they don't trigger 979
				// and this check owns them. either way, proper lby breakers will bypass this, so 
				// we have to improvise; (L236)

				// we detected a lowerbody update
				if( pRecord->m_flLowerBodyYawTarget != pPreviousRecord->m_flLowerBodyYawTarget ) {
					// check if the update we detected exceeds the "tolerance" body update delta 
					// that could mark this update as a lowerbody flick.
					if( fabs( Math::AngleDiff( pRecord->m_flLowerBodyYawTarget, pPreviousRecord->m_flLowerBodyYawTarget ) ) > XorFlt( 30.f ) ) {
						// since the lby angle change exceeded 35 degrees delta, it most likely means
						// that the enemy attempted to break lby. at this point we can assume that 
						// they're breaking lby.

						resolverData.m_bBreakingLowerBody = true;
						resolverData.m_flLastUpdateTime = pRecord->m_flAnimationTime;

						// mark current breaker type as low.
						resolverData.m_eBodyBreakType = EBodyBreakTypes::BREAK_LOW;
					}
				}
				// no body update was detected in a while..
				// they are either not breaking lby, or successfully breaking lby now (after failing before).
				else if( fabs( resolverData.m_flLastUpdateTime - pRecord->m_flAnimationTime ) > 1.125f ) {
					// notify the cheat that we're going to be using
					// logic in order to find out wtf is goin on
					resolverData.m_bHasntUpdated = true;

					const float flDeltaTolerance = XorFlt(35.f);
					bool bIsNearLastMoveLBY = false;

					// Check if last move data is valid and recent enough geographically
					if (!resolverData.m_vecLastMoveOrigin.IsZero() &&
						(resolverData.m_vecLastMoveOrigin - pRecord->m_vecOrigin).Length() <= XorFlt(128.f)) {
						// Check if current LBY is close to the LBY when they last stopped moving
						if (fabs(Math::AngleDiff(pRecord->m_flLowerBodyYawTarget, resolverData.m_flLastMoveBody)) <= flDeltaTolerance) {
							bIsNearLastMoveLBY = true;
						}
					}

					if (bIsNearLastMoveLBY) {
						// If LBY is similar to the LBY they had when they last stopped moving,
						// and we haven't detected an LBY break for a while, assume they are not breaking.
						resolverData.m_bBreakingLowerBody = false;
						resolverData.m_eBodyBreakType = EBodyBreakTypes::BREAK_NONE;
					} else {
						// Otherwise, if LBY isn't near their last moving LBY (or last move data is not reliable),
						// and we're in the "hasn't updated" state (long time since LBY break detected, 979 anim quiet),
						// assume they are subtly breaking LBY or holding an angle different from their post-movement LBY.
						resolverData.m_bBreakingLowerBody = true;
						resolverData.m_eBodyBreakType = EBodyBreakTypes::BREAK_LOW;
					}
				}
			}
		}
	}

	// to make sure that everything went right incase something
	// before went wrong, we should see ifthe enemies lby hasn't
	// updated in a while, and if that is the case we can assume
	// that they are no longer breaking lby.

	// we don't want to run this when we're comparing the lby against
	// the logical freestand angle, since it will overwrite our results.
	if( !resolverData.m_bHasntUpdated ) {
		if( ( pRecord->m_fFlags & FL_ONGROUND ) && ( pRecord->m_vecVelocity.Length( ) <= 0.1f || ( pRecord->m_bIsFakewalking ) ) ) {
			if( fabs( resolverData.m_flLastUpdateTime - pRecord->m_flAnimationTime ) > 1.125f ) {
				resolverData.m_bBreakingLowerBody = false;
				resolverData.m_eBodyBreakType = EBodyBreakTypes::BREAK_NONE;
			}
		}
	}
}

void Resolver::ResolveHeight( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );
	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	QAngle angAway;
	pLocal->IsDead( ) ? Vector( 0, 180, 0 ) : Math::VectorAngles( pLocal->m_vecOrigin( ) - pRecord->m_vecOrigin, angAway );

	switch( resolverData.m_iMissedShots % 4 ) {
		case 0:
			// if there is a valid edge angle, apply it to the player
			// shoutout to all supremacy edge users trololololol
			if( g_AntiAim.DoEdgeAntiAim( pRecord->m_pEntity, resolverData.m_angEdgeAngle ) ) {
				pRecord->m_angEyeAngles.y = resolverData.m_angEdgeAngle.y;
			}
			// if not, just do away + 180 (so head will face towards us)
			else {
				pRecord->m_angEyeAngles.y = angAway.y + 180.f;
			}
			break;
		case 1:
			// if there is a valid freestand angle, use it
			if( resolverData.m_flFreestandYaw != FLT_MAX ) {
				pRecord->m_angEyeAngles.y = resolverData.m_flFreestandYaw;
			}
			// if not, use away angle (head facing away from us)
			else {
				pRecord->m_angEyeAngles.y = angAway.y;
			}
			break;
			// from here on now it's just -+90.f to away angle (so sideways xd)
		case 2:
			pRecord->m_angEyeAngles.y = angAway.y - 90.f;
			break;
		case 3:
			pRecord->m_angEyeAngles.y = angAway.y + 90.f;
			break;
	}
}

void Resolver::ResolveStand( AnimationRecord* pRecord, AnimationRecord* pPreviousRecord ) {
	auto& resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex() );
	auto pLocal = C_CSPlayer::GetLocalPlayer(); // For away angle if needed

	// --- Edge Detection ---
	float wallDistance = -1.f;
	QAngle wallAngle(0,0,0);
	bool isNearWall = IsNearWall(pRecord->m_pEntity, wallDistance, wallAngle);
	// Store this info in resolverData if it needs to be accessed by other resolver stages or for logging misses
	// resolverData.m_bIsNearWall = isNearWall; 
	// resolverData.m_angWallAngle = wallAngle;

	// Store this info in resolverData 
	 resolverData.m_bIsNearWall_ForBrute = isNearWall; 
	 resolverData.m_angWallAngle_ForBrute = isNearWall ? wallAngle : QAngle(0,0,0);

	// Initial resolver type
	resolverData.m_iCurrentResolverType = 10; // Base Stand Resolve

	// If we have no movement data, try LBY first, then bruteforce.
	if ( resolverData.m_flLastMoveBody == FLT_MAX && resolverData.m_flLastMoveAnimTime == FLT_MAX && resolverData.m_vecLastMoveOrigin.IsZero( ) ) {
		// if they have breaking lby, try to use plausible angles based on that first.
		if ( resolverData.m_bBreakingLowerBody && resolverData.m_eBodyBreakType != BREAK_NONE ) {
			std::vector<float> plausible_angles;
			float base_lby = pRecord->m_flLowerBodyYawTarget;
			plausible_angles.push_back(base_lby); // LBY itself

			if (resolverData.m_eBodyBreakType == BREAK_LOW) {
				plausible_angles.push_back(Math::AngleNormalize(base_lby - 60.f));
				plausible_angles.push_back(Math::AngleNormalize(base_lby + 60.f));
				plausible_angles.push_back(Math::AngleNormalize(base_lby - 90.f));
				plausible_angles.push_back(Math::AngleNormalize(base_lby + 90.f));
			} else if (resolverData.m_eBodyBreakType == BREAK_HIGH) {
				plausible_angles.push_back(Math::AngleNormalize(base_lby - 35.f));
				plausible_angles.push_back(Math::AngleNormalize(base_lby + 35.f));
			}

			for (float ang : plausible_angles) {
				if (IsAnglePlausible(pRecord, ang, isNearWall, wallAngle)) {
					pRecord->m_angEyeAngles.y = ang;
					resolverData.m_iCurrentResolverType = 11; // Stand Resolve - Plausible LBY Break
					return;
				}
			}
			// Fallback to brute if plausible angles for break didn't work.
		}

		// Fallback to LBY if no specific break detected or plausible angles failed
		if (IsAnglePlausible(pRecord, pRecord->m_flLowerBodyYawTarget, isNearWall, wallAngle)) {
			pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;
			resolverData.m_iCurrentResolverType = 12; // Stand Resolve - LBY
			return;
		}
		
		ResolveBrute( pRecord );
		resolverData.m_iCurrentResolverType = 101; // Stand Resolve - Brute (No Move Data)
		return;
	}

	// Freestanding / Edge priority
	if (isNearWall) {
		pRecord->m_angEyeAngles.y = wallAngle.y;
		if (IsAnglePlausible(pRecord, wallAngle.y, true, wallAngle)) {
			resolverData.m_iCurrentResolverType = 13; // Stand Resolve - Edge Plausible
			return;
		}
		// If basic edge is not plausible, maybe try offsets or brute?
	} else {
		float flFreestandYaw = ResolveAntiFreestand(pRecord);
		if (IsAnglePlausible(pRecord, flFreestandYaw)) {
			pRecord->m_angEyeAngles.y = flFreestandYaw;
			resolverData.m_iCurrentResolverType = 14; // Stand Resolve - Freestand Plausible
			return;
		}
	}


	// Different scenarios based on LBY break type.
	switch ( resolverData.m_eBodyBreakType ) {
	case BREAK_NONE:
	{
		// No specific LBY break detected.
		// Prioritize last moving LBY if plausible and significantly different from current LBY.
		if ( resolverData.m_flLastMoveBody != FLT_MAX && std::abs( Math::AngleDiff( pRecord->m_flLowerBodyYawTarget, resolverData.m_flLastMoveBody ) ) > 30.f ) {
			if ( IsAnglePlausible( pRecord, resolverData.m_flLastMoveBody, isNearWall, wallAngle ) ) {
				pRecord->m_angEyeAngles.y = resolverData.m_flLastMoveBody;
				resolverData.m_iCurrentResolverType = 15; // Stand Resolve - Last Move LBY
				return;
			}
		}

		// Then, try current LBY.
		if ( IsAnglePlausible( pRecord, pRecord->m_flLowerBodyYawTarget, isNearWall, wallAngle ) ) {
			pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;
			resolverData.m_iCurrentResolverType = 16; // Stand Resolve - Current LBY
			return;
		}
		break;
	}
	case BREAK_LOW:
	{
		// Low LBY break (e.g., classic desync, flicking significantly)
		// Try angles around LBY that are common for low breaks.
		std::vector<float> low_break_offsets = { 0.f, -60.f, 60.f, -90.f, 90.f, -110.f, 110.f }; // Example offsets
		for ( float offset : low_break_offsets ) {
			float test_angle = Math::AngleNormalize( pRecord->m_flLowerBodyYawTarget + offset );
			if ( IsAnglePlausible( pRecord, test_angle, isNearWall, wallAngle ) ) {
				pRecord->m_angEyeAngles.y = test_angle;
				resolverData.m_iCurrentResolverType = 17; // Stand Resolve - Low LBY Break Offset
				return;
			}
		}
		break;
	}
	case BREAK_HIGH:
	{
		// High LBY break (e.g., smaller desync, perhaps jitter)
		// Try angles closer to LBY.
		std::vector<float> high_break_offsets = { 0.f, -35.f, 35.f, -25.f, 25.f }; // Example offsets
		for ( float offset : high_break_offsets ) {
			float test_angle = Math::AngleNormalize( pRecord->m_flLowerBodyYawTarget + offset );
			if ( IsAnglePlausible( pRecord, test_angle, isNearWall, wallAngle ) ) {
				pRecord->m_angEyeAngles.y = test_angle;
				resolverData.m_iCurrentResolverType = 18; // Stand Resolve - High LBY Break Offset
				return;
			}
		}
		break;
	}
	}

	// Fallback logic if specific break type strategies didn't yield a plausible angle.
	// Consider missed shots.
	if ( resolverData.m_iMissedShots > 1 ) {
		// If missed LBY-related shots, try something else.
		// Try freestanding / away.
		QAngle angAway = QAngle(0,0,0);
		if(pLocal && !pLocal->IsDead())
			Math::VectorAngles(pRecord->m_pEntity->m_vecOrigin() - pLocal->m_vecOrigin(), angAway);

		float flFreestandYaw = ResolveAntiFreestand(pRecord);

		if (IsAnglePlausible(pRecord, flFreestandYaw)) {
			pRecord->m_angEyeAngles.y = flFreestandYaw;
			resolverData.m_iCurrentResolverType = 19; // Stand Resolve - Freestand (Missed Shots)
			return;
		}
		if (IsAnglePlausible(pRecord, angAway.y)) {
			pRecord->m_angEyeAngles.y = angAway.y;
			resolverData.m_iCurrentResolverType = 20; // Stand Resolve - Away (Missed Shots)
			return;
		}
	}

	// If still nothing, default to bruteforce.
	ResolveBrute( pRecord );
	resolverData.m_iCurrentResolverType = 102; // Stand Resolve - Brute (Fallback)
}

void Resolver::ResolveMove( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );

	// Store the LBY, origin, and animation time from the current moving record.
	// This data is crucial for the ResolveStand logic when the player stops.
	resolverData.m_flLastMoveBody = pRecord->m_flLowerBodyYawTarget;
	resolverData.m_vecLastMoveOrigin = pRecord->m_vecOrigin;
	resolverData.m_flLastMoveAnimTime = pRecord->m_flAnimationTime;

	// Set a timer for LBY realignment prediction for when the player might stop.
	// This is used by UpdateLBYPrediction if the player stops moving shortly after.
	resolverData.m_flLowerBodyRealignTimer = pRecord->m_flAnimationTime + XorFlt( 0.22f );

	// If the player is moving with significant velocity, reset the LBY missed shots counter.
	// This assumes that LBY is generally reliable during true movement.
	// Threshold XorFlt(35.f) can be adjusted if needed, e.g., lower for more frequent resets
	// or higher to avoid resets on very slow/subtle movements.
	if( pRecord->m_vecVelocity.Length2D( ) >= XorFlt( 35.f ) ) // Default was 35.f
		resolverData.m_iMissedShotsLBY = 0;

	resolverData.m_iCurrentResolverType = 19; // Indicates moving resolve mode.

	// When moving, LBY prediction stage is typically not active in the same way as standing.
	// LBY should follow the movement direction.
	resolverData.m_bInPredictionStage = false;

	// For moving players, the primary resolved angle is their LBY.
	pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;
}

void Resolver::ResolveAir( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );

	// Calculate yaw based on velocity direction and angle away from local player.
	const float flVelocityDirYaw = RAD2DEG( std::atan2( pRecord->m_vecVelocity.y, pRecord->m_vecVelocity.x ) ); // Corrected atan2 arguments order

	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	QAngle angAway;
	// If local player is dead, use a default away vector (0,180,0), otherwise calculate actual away angle.
	Math::VectorAngles( pLocal->IsDead() ? Vector(0,0,0) : pRecord->m_vecOrigin - pLocal->m_vecOrigin(), angAway );

	const float flXored180 = XorFlt( 180.f );
	const float flXored90 = XorFlt( 90.f );

	switch( resolverData.m_iMissedShots % 6 ) { // Increased modulo for more states
		case 0: // Base LBY (less reliable in air, but a starting point)
			resolverData.m_iCurrentResolverType = 200;
			pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;
			break;
		case 1: // Backwards to velocity direction
			resolverData.m_iCurrentResolverType = 20;
			pRecord->m_angEyeAngles.y = flVelocityDirYaw + flXored180;
			break;
		case 2: // Freestand or Away
			resolverData.m_iCurrentResolverType = 21;
			if( resolverData.m_flFreestandYaw != FLT_MAX ) {
				pRecord->m_angEyeAngles.y = SnapToClosestYaw( resolverData.m_flFreestandYaw );
			}
			else {
				pRecord->m_angEyeAngles.y = SnapToClosestYaw( angAway.y );
			}
			break;
		case 3: // Last Move LBY or Away + 180
			resolverData.m_iCurrentResolverType = 22;
			if( resolverData.m_flLastMoveBody != FLT_MAX ) {
				pRecord->m_angEyeAngles.y = SnapToClosestYaw( resolverData.m_flLastMoveBody );
			}
			else {
				pRecord->m_angEyeAngles.y = SnapToClosestYaw( angAway.y + flXored180 );
			}
			break;
		case 4: // Sideways to velocity direction (left)
			resolverData.m_iCurrentResolverType = 23;
			pRecord->m_angEyeAngles.y = flVelocityDirYaw - flXored90;
			break;
		case 5: // Sideways to velocity direction (right)
			resolverData.m_iCurrentResolverType = 24;
			pRecord->m_angEyeAngles.y = flVelocityDirYaw + flXored90;
			break;
	}
}

void Resolver::ResolveAirUntrusted( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );

	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	QAngle angAway;
	pLocal->IsDead( ) ? Vector( 0, 180, 0 ) : Math::VectorAngles( pLocal->m_vecOrigin( ) - pRecord->m_vecOrigin, angAway );

	switch( resolverData.m_iMissedShots % 9 ) {
		case 0:
			pRecord->m_angEyeAngles.y = angAway.y + 180.f;
			resolverData.m_iCurrentResolverType = 40;
			break;
		case 1:
			pRecord->m_angEyeAngles.y = angAway.y + 150.f;
			resolverData.m_iCurrentResolverType = 41;
			break;
		case 2:
			pRecord->m_angEyeAngles.y = angAway.y - 150.f;
			resolverData.m_iCurrentResolverType = 42;
			break;
		case 3:
			pRecord->m_angEyeAngles.y = angAway.y + 165.f;
			resolverData.m_iCurrentResolverType = 43;
			break;
		case 4:
			pRecord->m_angEyeAngles.y = angAway.y - 165.f;
			resolverData.m_iCurrentResolverType = 44;
			break;
		case 5:
			pRecord->m_angEyeAngles.y = angAway.y + 135.f;
			resolverData.m_iCurrentResolverType = 45;
			break;
		case 6:
			pRecord->m_angEyeAngles.y = angAway.y - 135.f;
			resolverData.m_iCurrentResolverType = 46;
			break;
		case 7:
			pRecord->m_angEyeAngles.y = angAway.y + 90.f;
			resolverData.m_iCurrentResolverType = 47;
			break;
		case 8:
			pRecord->m_angEyeAngles.y = angAway.y - 90.f;
			resolverData.m_iCurrentResolverType = 48;
			break;
		default:
			break;
	}
}

void Resolver::ResolvePred( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );
	auto pLocal = C_CSPlayer::GetLocalPlayer( ); // Needed for angAway

	// This mode is typically entered when UpdateLBYPrediction has detected conditions
	// indicating an LBY flick is imminent or occurring (pRecord->m_bLBYFlicked should be true).

	// Keep m_bInPredictionStage true, as we are actively in the predicted flick window.
	// UpdateLBYPrediction will continue to manage this state for subsequent ticks.
	resolverData.m_bInPredictionStage = true; 

	const float FLICK_TO_HEAD_OFFSET_POSITIVE = XorFlt(58.f); // Common offset for flicking head (can be tuned)
	const float FLICK_TO_HEAD_OFFSET_NEGATIVE = XorFlt(-58.f); // Common offset for flicking head (can be tuned)

	switch (resolverData.m_iMissedShotsLBY % 4) { // Cycle through 4 strategies based on misses in LBY prediction mode
		case 0:
			// Aggressively try positive offset from LBY, common for exposing head during flick
			pRecord->m_angEyeAngles.y = Math::AngleNormalize(pRecord->m_flLowerBodyYawTarget + FLICK_TO_HEAD_OFFSET_POSITIVE);
			resolverData.m_iCurrentResolverType = 250; // Pred: LBY + Offset
			break;
		case 1:
			// Aggressively try negative offset from LBY
			pRecord->m_angEyeAngles.y = Math::AngleNormalize(pRecord->m_flLowerBodyYawTarget + FLICK_TO_HEAD_OFFSET_NEGATIVE);
			resolverData.m_iCurrentResolverType = 251; // Pred: LBY - Offset
			break;
		case 2:
			// Fallback to pure LBY if aggressive angles missed
			pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;
			resolverData.m_iCurrentResolverType = 25; // Pred: Pure LBY
			break;
		case 3:
			// Further fallback: if even LBY missed, try Freestand or Away (similar to ResolveLogic fallback)
			resolverData.m_iCurrentResolverType = 252; // Pred: Freestand/Away Fallback
			if (pLocal && resolverData.m_flFreestandYaw != FLT_MAX) {
				pRecord->m_angEyeAngles.y = SnapToClosestYaw(resolverData.m_flFreestandYaw);
			}
			else if (pLocal) {
				QAngle angAway;
				Math::VectorAngles( pRecord->m_vecOrigin - pLocal->m_vecOrigin(), angAway );
				pRecord->m_angEyeAngles.y = SnapToClosestYaw( angAway.y + XorFlt(180.f) ); // Face towards local
			}
			else { // Should not happen if pLocal is valid, but as a safety net
				pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;
			}
			break;
	}
}

void Resolver::ResolveLogic( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );

	// This mode is entered when m_bDesyncFlicking is true, meaning the player
	// is likely rapidly changing their real yaw to desynchronize their LowerBodyYawTarget.
	// Targeting LBY directly is often ineffective in this state.

	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	// auto pState = pRecord->m_pEntity->m_PlayerAnimState( );
	// if( !pState )
	// 	return;

	resolverData.m_iCurrentResolverType = 420; // Indicates Desync Logic resolve mode.

	// Define a common desync angle magnitude. ~58 degrees is a common value.
	const float FLICK_REAL_VS_LBY_DELTA = XorFlt(58.f);

	// Based on missed shots in this desync mode, try different sides of the LBY.
	switch( resolverData.m_iMissedShotsDesync % 3 ) { // Added a third state for fallback
		case 0: // Try one side of LBY (e.g., LBY + delta)
			pRecord->m_angEyeAngles.y = Math::AngleNormalize( pRecord->m_flLowerBodyYawTarget + FLICK_REAL_VS_LBY_DELTA );
			resolverData.m_iCurrentResolverType = 421;
			break;
		case 1: // Try the other side of LBY (e.g., LBY - delta)
			pRecord->m_angEyeAngles.y = Math::AngleNormalize( pRecord->m_flLowerBodyYawTarget - FLICK_REAL_VS_LBY_DELTA );
			resolverData.m_iCurrentResolverType = 422;
			break;
		case 2: // Fallback: if desync flicks are hard to hit, try something more stable like Freestand or Away.
			resolverData.m_iCurrentResolverType = 423;
			if (resolverData.m_flFreestandYaw != FLT_MAX) {
				pRecord->m_angEyeAngles.y = SnapToClosestYaw(resolverData.m_flFreestandYaw);
			}
			else {
				QAngle angAway;
				Math::VectorAngles( pRecord->m_vecOrigin - pLocal->m_vecOrigin(), angAway );
				pRecord->m_angEyeAngles.y = SnapToClosestYaw( angAway.y + XorFlt(180.f) ); // Face towards local
			}
			break;
	}

	// The previous logic directly manipulating pState->m_flFootYaw might be too complex
	// or have unintended side effects without a full animstate recalculation within the resolver.
	// This direct approach focuses on setting a plausible real yaw based on typical desync behavior.
	/*
	// force lby here
	pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;

	// negate the footyaw
	const float flFootYawDelta = pState->m_flFootYaw - pState->m_flEyeYaw;

	// bruteforce incase our calculation went wrong
	switch( resolverData.m_iMissedShotsDesync % 2 ) {
		case 0:
			pState->m_flFootYaw = pState->m_flEyeYaw - flFootYawDelta;
			break;
		case 1:
			pState->m_flFootYaw = pState->m_flEyeYaw + flFootYawDelta;
			break;
	}
	*/
}

// FATALITY XDDDDDD
void Resolver::ResolveOverride( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );
	resolverData.m_bOverriding = false; // Reset override flag at the beginning

	C_CSPlayer *pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal || pLocal->IsDead() ) // Also check if local player is dead
		return;

	if( !g_Vars.rage.antiaim_resolver_override )
		return;

	// Static vector to store potential targets in FOV, cleared each frame essentially by time check.
	static std::vector<C_CSPlayer *> targets;
	static auto last_checked_override_time = 0.f;

	QAngle viewangles;
	g_pEngine->GetViewAngles( viewangles );

	// Populate targets list if current time differs from last check time (effectively, once per frame or tick group)
	if( last_checked_override_time != g_pGlobalVars->curtime ) {
		last_checked_override_time = g_pGlobalVars->curtime;
		targets.clear( );

		const float needed_fov = XorFlt(20.f); // FOV threshold for considering a target for override
		for( auto i = 1; i <= g_pGlobalVars->maxClients; i++ ) {
			auto ent = ( C_CSPlayer * )g_pEntityList->GetClientEntity( i );
			if( !ent || ent->IsDead( ) || ent->IsDormant( ) || ent == pLocal || !ent->IsPlayer() || ent->m_iTeamNum() == pLocal->m_iTeamNum())
				continue;

			const float fov = Math::GetFov( viewangles, pLocal->GetEyePosition( ), ent->WorldSpaceCenter( ) );
			if( fov < needed_fov ) {
				targets.push_back( ent );
			}
		}
	}

	bool current_record_is_override_target = false;
	for( auto &target : targets ) {
		if( pRecord->m_pEntity == target ) {
			current_record_is_override_target = true;
			break;
		}
	}

	if( !current_record_is_override_target )
		return; // Current pRecord entity is not in the override target list

	// Static variables to help stabilize override when aim is steady
	static auto last_delta_override = 0.f;
	static auto last_angle_override = 0.f;
	static bool had_target_last_frame = false;

	const float at_target_yaw = Math::CalcAngle( pLocal->m_vecOrigin( ), pRecord->m_vecOrigin ).y;
	float LBYDelta = Math::AngleNormalize( viewangles.y - at_target_yaw );

	// If we had a target last frame and our aim hasn't changed much, use previous delta to prevent jitter.
	if( had_target_last_frame && fabsf( viewangles.y - last_angle_override ) < XorFlt(0.1f) && pRecord->m_pEntity == targets.front() /*Ensure it is the same primary target*/ ) {
		LBYDelta = last_delta_override;
	}

	had_target_last_frame = true; // Mark that we have an override target this frame
	last_angle_override = viewangles.y; // Store current aim angle
	last_delta_override = LBYDelta;    // Store current delta

	resolverData.m_iCurrentResolverType = 26; // Indicates override resolve mode.

	const float override_threshold = XorFlt(4.0f);
	if( LBYDelta >= override_threshold ) // Aiming to the right of the target
		pRecord->m_angEyeAngles.y = Math::AngleNormalize( at_target_yaw + XorFlt(90.f) );
	else if( LBYDelta <= -override_threshold ) // Aiming to the left of the target
		pRecord->m_angEyeAngles.y = Math::AngleNormalize( at_target_yaw - XorFlt(90.f) );
	else // Aiming close to the target
		pRecord->m_angEyeAngles.y = Math::AngleNormalize( at_target_yaw );

	resolverData.m_bOverriding = true; // Set the override flag for this record
}

float Resolver::ResolveAntiFreestand( AnimationRecord *pRecord ) {
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );

	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal || pLocal->IsDead() )
		return FLT_MAX;

	// Constants for freestanding logic
	const float flProjectionDistance = XorFlt( 32.f ); // How far to project the enemy's head position for testing
	const float flTraceStepSize = XorFlt( 4.f );    // Step size for iterating along the trace line
	const float flSideAngleOffset = XorFlt( 90.f ); // Offset for side angles (left/right)

	QAngle angAway;
	Math::VectorAngles( pRecord->m_vecOrigin - pLocal->m_vecOrigin(), angAway ); // Calculate angle from local player to enemy

	auto enemy_eyepos = pRecord->m_pEntity->GetEyePosition( );

	// Define a set of angles to test for freestanding
	// 1. Directly away from the local player (enemy's back towards local player)
	// 2. Enemy's right side towards local player
	// 3. Enemy's left side towards local player
	std::vector< AdaptiveAngle > angles_to_test{ };
	angles_to_test.emplace_back( angAway.y );
	angles_to_test.emplace_back( Math::AngleNormalize(angAway.y + flSideAngleOffset) );
	angles_to_test.emplace_back( Math::AngleNormalize(angAway.y - flSideAngleOffset) );

	// Starting point for traces is the local player's eye position.
	auto trace_start_pos = pLocal->GetEyePosition( );

	bool bValidPathFound = false; // Flag to indicate if any path had obstructions

	// Iterate through each defined angle to test for obstructions
	for( auto &current_angle_data : angles_to_test ) {

		// Compute the 'rough' estimated head position of the enemy if they were facing current_angle_data.m_yaw
		Vector estimated_enemy_head_pos{
			enemy_eyepos.x + std::cos( DEG2RAD( current_angle_data.m_yaw ) ) * flProjectionDistance,
			enemy_eyepos.y + std::sin( DEG2RAD( current_angle_data.m_yaw ) ) * flProjectionDistance,
			enemy_eyepos.z
		};

		// Compute the direction vector from local player to the estimated enemy head position
		Vector trace_direction = estimated_enemy_head_pos - trace_start_pos;
		float trace_length = trace_direction.Normalize( ); // Normalize also returns original length

		// Should never happen if positions are valid
		if( trace_length <= 0.f )
			continue;

		// Step through the trace path, checking for obstructions
		for( float i{ 0.f }; i < trace_length; i += flTraceStepSize ) {
			// Get the current point along the trace line
			Vector current_trace_point = trace_start_pos + ( trace_direction * i );

			// Get the contents at this point (checking for solid parts that block shots)
			int point_contents = g_pEngineTrace->GetPointContents( current_trace_point, MASK_SHOT_HULL );

			// If the point does not contain anything that can stop a bullet, continue to the next step
			if( !( point_contents & MASK_SHOT_HULL ) )
				continue;

			// If an obstruction is found, increment the 'obstruction distance' for this angle.
			// g_AntiAim.UpdateFreestandPriority likely weights closer obstructions more heavily.
			current_angle_data.m_dist += ( flTraceStepSize * g_AntiAim.UpdateFreestandPriority( trace_length, i ) );

			// Mark that we found at least one obstructed path
			bValidPathFound = true;
		}
	}

	// If no obstructions were found on any of the tested paths, freestand logic cannot determine a best angle.
	if( !bValidPathFound ) {
		return FLT_MAX; // Return an invalid angle indicator
	}

	// Sort the angles by their 'obstruction distance' in descending order.
	// The angle with the highest m_dist is considered the most occluded.
	std::sort( angles_to_test.begin( ), angles_to_test.end( ),
			   [ ] ( const AdaptiveAngle &a, const AdaptiveAngle &b ) {
		return a.m_dist > b.m_dist;
	} );

	// The best angle (most occluded) should be at the front of the container now.
	// This is the angle the enemy is likely trying to hide by placing a wall towards it.
	AdaptiveAngle *best_freestand_angle = &angles_to_test.front( );

	return Math::AngleNormalize( best_freestand_angle->m_yaw );
}

// This is the PlayerList-based OverrideResolver, called at the end of ResolvePlayers.
// It allows for final adjustments to the resolved angles based on per-player settings.
void Resolver::OverrideResolver( AnimationRecord *pRecord ) {
	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	// Ensure local player is valid and alive; if not, no override logic can be applied.
	if( !pLocal || pLocal->IsDead() ) 
		return;

	// Get references to resolver data for the current entity and their player list settings.
	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );
	auto playerListInfo = g_PlayerList.GetSettings( pRecord->m_pEntity->GetSteamID( ) );

	// --- Pitch Override --- 
	// If player list settings dictate a forced pitch, apply it.
	if( playerListInfo.m_bForcePitch ) {
		pRecord->m_angEyeAngles.x = playerListInfo.m_flForcedPitch;
		// Note: A specific resolver type for pitch override could be added if granular tracking is needed.
		// resolverData.m_iCurrentResolverType = 1370; // Example: PlayerList: Pitch Override
	}

	// --- Edge Correction Override --- 
	// (Currently commented out - requires g_AntiAim.HandleEdge to be fully reviewed and functional)
	// If enabled, this would override yaw to an angle detected by edge detection logic.
	/* 
	if( playerListInfo.m_bEdgeCorrection ) {
		// Example: std::pair<bool, float> edgeData = g_AntiAim.HandleEdge( pRecord->m_pEntity ); 
		// const bool bOnGround = ( pRecord->m_fFlags & FL_ONGROUND );
		// const bool bIsStatic = pRecord->m_vecVelocity.Length2D() <= XorFlt(0.1f);
		// if( edgeData.first && bOnGround && bIsStatic && edgeData.second != FLT_MAX ) {
		// pRecord->m_angEyeAngles.y = Math::AngleNormalize(edgeData.second);
		// resolverData.m_iCurrentResolverType = 1338; // PlayerList: Edge Correction Override
		// return; // If Edge Correction takes full precedence, might return here.
		// }
	}
	*/

	// --- Yaw Override --- 
	// If no specific yaw forcing option is selected in player list, exit this override function.
	if( !playerListInfo.m_iForceYaw ) {
		return;
	}

	// Default to current yaw from previous resolver stages; will be overwritten by specific cases below.
	float flForcedYawResult = pRecord->m_angEyeAngles.y; 

	// Apply yaw override based on the selected mode in player list settings.
	switch (playerListInfo.m_iForceYaw) {
		case 1: // Static Yaw: Apply a fixed yaw value specified in playerListInfo.
			flForcedYawResult = playerListInfo.m_flForcedYaw;
			resolverData.m_iCurrentResolverType = 1371; // PlayerList: Static Yaw
			break;

		case 2: // Away from Local Player (+ offset): Angle enemy away from local player, with an additional offset from playerListInfo.
		{
			const float flAngleToLocal = Math::CalcAngle( pRecord->m_pEntity->m_vecOrigin( ), pLocal->m_vecOrigin( ) ).y;
			flForcedYawResult = flAngleToLocal + playerListInfo.m_flForcedYaw; // m_flForcedYaw acts as an offset here.
			resolverData.m_iCurrentResolverType = 1372; // PlayerList: Away + Offset
			break;
		}

		case 3: // Lower Body Yaw (+ offset): Angle based on enemy's LBY, with an additional offset from playerListInfo.
			flForcedYawResult = pRecord->m_flLowerBodyYawTarget + playerListInfo.m_flForcedYaw;
			resolverData.m_iCurrentResolverType = 1373; // PlayerList: LBY + Offset
			break;

		case 4: // Towards Nearest Enemy (+ offset): (Not Implemented)
			// TODO: Implement logic to find the nearest enemy to pRecord->m_pEntity.
			// This would involve iterating players, checking teams, distances, then calculating angle to the nearest valid enemy.
			// Example: flForcedYawResult = GetAngleToNearestEnemy(pRecord->m_pEntity, pLocal) + playerListInfo.m_flForcedYaw;
			resolverData.m_iCurrentResolverType = 1374; // PlayerList: Nearest Enemy (Placeholder)
			// As a fallback if not implemented, could use LBY or another known angle for now.
			flForcedYawResult = pRecord->m_flLowerBodyYawTarget; // Fallback to LBY if not implemented
			break;

		case 5: // Towards Average Enemy Position (+ offset): (Not Implemented)
			// TODO: Implement logic to find the average position of multiple (e.g., all) enemies.
			// This involves collecting positions and calculating a centroid, then the angle to it.
			// Example: flForcedYawResult = GetAngleToAverageEnemyPos(pRecord->m_pEntity, pLocal) + playerListInfo.m_flForcedYaw;
			resolverData.m_iCurrentResolverType = 1375; // PlayerList: Average Enemy (Placeholder)
			// As a fallback if not implemented, could use LBY or another known angle for now.
			flForcedYawResult = pRecord->m_flLowerBodyYawTarget; // Fallback to LBY if not implemented
			break;

		default: // Default case if m_iForceYaw has an unexpected value.
			resolverData.m_iCurrentResolverType = 1337; // PlayerList: General/Unknown Yaw Override
			// Fallback to a known safe value like LBY if an unknown m_iForceYaw is provided.
			flForcedYawResult = pRecord->m_flLowerBodyYawTarget;
			break;
	}

	// Apply and normalize the determined yaw angle.
	pRecord->m_angEyeAngles.y = Math::AngleNormalize( flForcedYawResult );
}

void Resolver::ResolvePlayers( AnimationRecord *pRecord, AnimationRecord *pPreviousRecord, AnimationRecord *pPenultimateRecord ) {
	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	if( !pRecord )
		return;

	auto &resolverData = m_arrResolverData.at( pRecord->m_pEntity->EntIndex( ) );

	player_info_t info;
	g_pEngine->GetPlayerInfo( pRecord->m_pEntity->EntIndex( ), &info );
	if( !g_Vars.rage.antiaim_correction )
		return;

	if( !g_Vars.rage.antiaim_resolver )
		return;

	if( info.fakeplayer ) {
		pRecord->m_bIsResolved = true;
		return;
	}

	// todo - maxwell; add a toggle for this somewhere..
	if( pRecord->m_iChokeTicks <= 1 ) {
		//pRecord->m_AnimationFlags |= ELagRecordFlags::RF_IsResolved;
		return;
	}

	if( g_PlayerList.GetSettings( pRecord->m_pEntity->GetSteamID( ) ).m_bDisableResolver ) {
		//pRecord->m_AnimationFlags |= ELagRecordFlags::RF_IsResolved;
		return;
	}

	resolverData.m_flFreestandYaw = ResolveAntiFreestand( pRecord );
	g_AntiAim.DoEdgeAntiAim( pRecord->m_pEntity, resolverData.m_angEdgeAngle );

	const float flViewDelta = fabs( pRecord->m_pEntity->GetEyePosition( ).z < pLocal->m_vecOrigin( ).z );
	// make sure player is not too far away (for instance you're on upper stairs mirage, shooting at a
	// player who's jungle, we don't want to handle him separatly)
	const float flOriginDelta = pRecord->m_vecOrigin.Distance( pLocal->m_vecOrigin( ) );
	resolverData.m_bHasHeightAdvantage = flOriginDelta < 25.f && flViewDelta > 5.f && pRecord->m_pEntity->GetEyePosition( ).z < pLocal->m_vecOrigin( ).z &&
		!resolverData.m_bSuppressHeightResolver;

	// update desync flick detection
	UpdateDesyncDetection( pRecord, pPreviousRecord, pPenultimateRecord );

	// update lby breaker detection
	UpdateBodyDetection( pRecord, pPreviousRecord );

	// reset lby flick
	pRecord->m_bLBYFlicked = false;

	// handle lby prediction
	// note - michal;
	// i'll let lby prediction run when they're 'not breaking lby'
	// so that we can see when they fail to break lby, it's more accurate
	UpdateLBYPrediction( pRecord );

	// handle resolver stages
	UpdateResolverStage( pRecord );

	switch( resolverData.m_eCurrentStage ) {
		case EResolverStages::RESOLVE_MODE_STAND:
			ResolveStand( pRecord, pPreviousRecord );
			break;
		case EResolverStages::RESOLVE_MODE_MOVE:
			ResolveMove( pRecord );
			break;
		case EResolverStages::RESOLVE_MODE_AIR:
			if( g_Vars.misc.anti_untrusted )
				ResolveAir( pRecord );
			else
				ResolveAirUntrusted( pRecord );
			break;
		case EResolverStages::RESOLVE_MODE_PRED:
			ResolvePred( pRecord );
			break;
		case EResolverStages::RESOLVE_MODE_OVERRIDE:
			ResolveOverride( pRecord );
			break;
		case EResolverStages::RESOLVE_MODE_LOGIC:
			ResolveLogic( pRecord );
			break;
	}

	// note - maxwell; do something else eventually, this will work for now. lolol.
	if( !g_Vars.misc.anti_untrusted ) {
		switch( resolverData.m_iMissedShots % 5 ) {
			case 0:
			case 1:
				pRecord->m_angEyeAngles.x = 89.f;
				break;
			case 2:
			case 3:
				pRecord->m_angEyeAngles.x = -89.f;
				break;
			case 4:
				pRecord->m_angEyeAngles.x = 0.f;
				break;
		}
	}

	// note - maxwell; this has to be called last.
	OverrideResolver( pRecord );

	// run this last. ;)
#if 1
	if( resolverData.m_flServerYaw != FLT_MAX && ( resolverData.m_flLastReceivedCheatDataTime > 0.f && fabs( resolverData.m_flLastReceivedCheatDataTime - TICKS_TO_TIME( g_pGlobalVars->tickcount ) ) < 2.5f ) && resolverData.m_eCurrentStage != EResolverStages::RESOLVE_MODE_PRED
		&& resolverData.m_eCurrentStage != EResolverStages::RESOLVE_MODE_MOVE ) {
		resolverData.m_iCurrentResolverType = 979;

		pRecord->m_angEyeAngles.y = resolverData.m_flServerYaw;
	}
#endif

	// normalize the angle (because of the bruteforce!!!!)
	pRecord->m_angEyeAngles.y = Math::AngleNormalize( pRecord->m_angEyeAngles.y );

	// resolve player
	pRecord->m_pEntity->m_angEyeAngles( ).y = pRecord->m_angEyeAngles.y;
	pRecord->m_pEntity->m_angEyeAngles( ).x = pRecord->m_angEyeAngles.x;

	// mark this player as resolved when resolver deems so
	pRecord->m_bIsResolved =
		( resolverData.m_eCurrentStage == EResolverStages::RESOLVE_MODE_MOVE ||
		  resolverData.m_eCurrentStage == EResolverStages::RESOLVE_MODE_PRED && ( pRecord->m_pEntity->m_fFlags( ) & FL_ONGROUND ) ) ||
		resolverData.m_iCurrentResolverType == 979;
}

bool Resolver::IsNearWall(C_CSPlayer* player, float& outWallDistance, QAngle& outWallAngle, float checkDistance) {
	if (!player || player->IsDead())
		return false;

	Vector playerEyePos = player->GetEyePosition(); // Or GetAbsOrigin() depending on desired check height
	Vector forward, right, up;
	QAngle playerAngles = player->m_angEyeAngles(); // Current eye angles, not super relevant for wall check but used for trace directions
	Math::AngleVectors(playerAngles, forward, right, up);

	float minFoundDistance = checkDistance + 1.f;
	bool wallFound = false;
	QAngle bestWallAngle;

	// Directions to check (forward, backward, left, right from player's perspective)
	// More directions can be added for better accuracy, e.g., diagonals
	std::vector<Vector> traceDirections = {
		forward, forward * -1.f, // Forward, Backward
		right, right * -1.f    // Right, Left
	};

	// Simplified trace for wall detection
	CTraceFilterSimple filter(player, COLLISION_GROUP_NONE); // Use CTraceFilterSimple and pass player to constructor to skip
	// filter.pSkip = player; // No longer needed if player is passed to constructor

	for (const auto& dir : traceDirections) {
		Vector traceEnd = playerEyePos + dir * checkDistance;
		Ray_t ray;
		ray.Init(playerEyePos, traceEnd);
		CGameTrace trace;
		g_pEngineTrace->TraceRay(ray, MASK_SOLID_BRUSHONLY, &filter, &trace);

		if (trace.fraction < 1.0f && trace.plane.normal.LengthSquared() > 0.9f) { // Hit something solid and got a valid plane normal
			float distToWall = trace.fraction * checkDistance;
			if (distToWall < minFoundDistance) {
				minFoundDistance = distToWall;
				Math::VectorAngles(trace.plane.normal, bestWallAngle); // Angle of the wall surface normal
				wallFound = true;
			}
		}
	}

	if (wallFound) {
		outWallDistance = minFoundDistance;
		outWallAngle = bestWallAngle;
		return true;
	}

	outWallDistance = -1.f;
	return false;
}