#include "SkinChanger.hpp"
#include "../../SDK/variables.hpp"
#include "../../SDK/Classes/weapon.hpp"
#include "../../SDK/Classes/player.hpp"
#include "../../SDK/Valve/CBaseHandle.hpp"
#include "KitParser.hpp"
#include "../../SDK/Classes/PropManager.hpp"
#include <algorithm>
#include <memory.h>
#include "../../SDK/Valve/recv_swap.hpp"
#include "../../Utils/extern/FnvHash.hpp"
#include "../../SDK/displacement.hpp"

static auto is_knife( const int i ) -> bool {
	return ( i >= WEAPON_KNIFE_BAYONET && i < GLOVE_STUDDED_BLOODHOUND ) || i == WEAPON_KNIFE_T || i == WEAPON_KNIFE;
}

static CHandle< C_BaseCombatWeapon > glove_handle{ };

SkinChanger g_SkinChanger;

void SkinChanger::Create( ) {
	RecvProp *prop = nullptr;
	Engine::g_PropManager.GetProp( XorStr( "DT_BaseViewModel" ), XorStr( "m_nSequence" ), &prop );
	m_sequence_hook = std::make_shared<RecvPropHook>( prop, &SequenceProxyFn );
}

void SkinChanger::Destroy( ) {
	m_sequence_hook->Unhook( );
	m_sequence_hook.reset( );
}

void SkinChanger::OnNetworkUpdate( bool start ) {
	auto &global = g_Vars.m_global_skin_changer;


	m_bSkipCheck = true;

	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !g_pEngine->IsConnected( ) ) {
		m_bSkipCheck = false;
		return;
	}

	if( !start ) {
		if( global.m_update_skins && !global.m_update_gloves ) {
			//float deltatime = m_pGlobalVars->realtime - lastSkinUpdate;
			//if( deltatime >= 0.2f ) {
			ForceItemUpdate( pLocal );
			global.m_update_skins = false;
			lastSkinUpdate = g_pGlobalVars->realtime;
			//}
		}

		if( ( !global.m_active || !global.m_glove_changer ) || global.m_update_gloves ) {
			auto glove = glove_handle.Get( );
			if( glove ) {
				auto networkable = glove->GetClientNetworkable( );
				if( networkable ) {
					networkable->SetDestroyedOnRecreateEntities( );
					networkable->Release( );
				}

				glove_handle.Set( nullptr );
			}

			const auto glove_config = GetDataFromIndex( global.m_gloves_idx );
			if( ( global.m_update_gloves && g_pGlobalVars->realtime - lastGloveUpdate >= 0.5f ) || ( glove_config && !glove_config->m_enabled && glove_config->m_executed ) ) {
				g_Vars.globals.m_bForceFullUpdate = true;

				if( global.m_update_gloves )
					lastGloveUpdate = g_pGlobalVars->realtime;

				global.m_update_gloves = false;

				if( glove_config )
					glove_config->m_executed = false;
			}
		}
		else if( pLocal && global.m_glove_changer ) {
			GloveChanger( pLocal );
		}

		m_bSkipCheck = false;
		return;
	}

	if( !global.m_active ) {
		m_bSkipCheck = false;
		return;
	}

	PostDataUpdateStart( pLocal );

	m_bSkipCheck = false;
}

void SkinChanger::PostDataUpdateStart( C_CSPlayer *local ) {
	if( !local )
		return;

	if( local->IsDead( ) )
		return;

	const auto local_index = local->EntIndex( );

	player_info_t player_info;
	if( !g_pEngine->GetPlayerInfo( local_index, &player_info ) )
		return;

	auto &global = g_Vars.m_global_skin_changer;

	// Handle weapon configs
	{
		auto weapons = local->m_hMyWeapons( );
		for( int i = 0; i < 48; ++i ) {
			auto weapon = ( C_BaseAttributableItem * )weapons[ i ].Get( );
			if( !weapon )
				continue;

			auto &definition_index = weapon->m_Item( ).m_iItemDefinitionIndex( );

			auto idx = is_knife( definition_index ) ? global.m_knife_idx : definition_index;

			const auto active_conf = GetDataFromIndex( idx );
			if( active_conf ) {
				if( ( !active_conf->m_enabled || !global.m_active ) && active_conf->m_executed )
					global.m_update_skins = true;

				ApplyConfigOnAttributableItem( weapon, active_conf, player_info.xuid_low );
			}
			else {
				EraseOverrideIfExistsByIndex( definition_index );
			}
		}
	}
	const auto view_model = ( C_BaseViewModel * )local->m_hViewModel( ).Get( );
	if( !view_model )
		return;

	const auto view_model_weapon = ( C_BaseAttributableItem * )view_model->m_hWeapon( ).Get( );
	if( !view_model_weapon )
		return;

	auto idx = view_model_weapon->m_Item( ).m_iItemDefinitionIndex( );
	if( g_KitParser.vecWeaponInfo.count( idx ) > 0 ) {
		const auto override_info = g_KitParser.vecWeaponInfo.at( idx );
		const auto override_model_index = g_pModelInfo->GetModelIndex( override_info.model );

		const auto weapon = reinterpret_cast< C_WeaponCSBaseGun * >( view_model_weapon );
		if( weapon ) {
			view_model->SetModelIndex( override_model_index );

			auto weapondata = weapon->GetCSWeaponData( );
			if( weapondata.IsValid( ) ) {
				const auto override_world_model_index = g_pModelInfo->GetModelIndex( weapondata->m_szWorldModel );
				const auto world_model = view_model_weapon->m_hWeaponWorldModel( ).Get( );
				if( world_model )
					world_model->SetModelIndex( override_world_model_index );
			}
		}
	}
}

void SkinChanger::DestroyGlove( ) {
	if( g_pEngine->IsConnected( ) || g_pEngine->IsInGame( ) )
		return;

	auto glove = glove_handle.Get( );
	if( glove ) {
		auto networkable = glove->GetClientNetworkable( );
		if( networkable ) {
			networkable->SetDestroyedOnRecreateEntities( );
			networkable->Release( );
		}

		glove_handle.Set( nullptr );
	}
}

void SkinChanger::EraseOverrideIfExistsByIndex( const int definition_index ) {
	if( g_KitParser.vecWeaponInfo.count( definition_index ) <= 0 )
		return;

	// We have info about the item not needed to be overridden
	const auto &original_item = g_KitParser.vecWeaponInfo.at( definition_index );
	auto &icon_override_map = m_icon_overrides;

	if( !original_item.icon )
		return;

	const auto override_entry = icon_override_map.find( original_item.icon );

	// We are overriding its icon when not needed
	if( override_entry != end( icon_override_map ) )
		icon_override_map.erase( override_entry ); // Remove the leftover override
}

void SkinChanger::ApplyConfigOnAttributableItem( C_BaseAttributableItem *attribute, CVariables::skin_changer_data *config, const unsigned xuid_low ) {
	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	if( !attribute )
		return;

	auto &item = attribute->m_Item( );
	auto &global = g_Vars.m_global_skin_changer;

	const int replaceKit = config->m_filter_paint_kits ? config->m_paint_kit : g_KitParser.vecPaintKits[ config->m_paint_kit_no_filter ].id;

	// Force fallback values to be used.
	item.m_iItemIDHigh( ) = -1;

	// Set the owner of the weapon to our lower XUID. (fixes StatTrak)
	item.m_iAccountID( ) = xuid_low;

	item.m_nFallbackPaintKit( ) = replaceKit;

	item.m_nFallbackSeed( ) = int( config->m_seed );

	item.m_iEntityQuality( ) = 0;

	if( int( config->m_stat_trak ) ) {
		item.m_nFallbackStatTrak( ) = int( config->m_stat_trak );

		item.m_iEntityQuality( ) = 9;
	}

	item.m_flFallbackWear( ) = ( config->m_wear != 0.f ) ? ( 100.f - config->m_wear ) / 100.f : config->m_wear;

	auto &definition_index = item.m_iItemDefinitionIndex( );

	auto &icon_override_map = m_icon_overrides;

	bool knife = is_knife( definition_index );

	int definition_override = 0;

	if( knife ) {
		if( global.m_knife_changer ) {
			definition_override = global.m_knife_idx;
			item.m_iEntityQuality( ) = 3;
		}
		else {
			// definition_override = ( pLocal->m_iTeamNum( ) == TEAM_CT ) ? WEAPON_KNIFE : WEAPON_KNIFE_T;
		}
	}
	else if( ( config->m_definition_index >= GLOVE_STUDDED_BLOODHOUND && config->m_definition_index <= GLOVE_HYDRA ) || config->m_definition_index == 4725 /*BROKEN FANG GLOVES*/ )
		definition_override = global.m_gloves_idx;

	if( definition_override && ( definition_override != definition_index ) ) {
		// We have info about what we gonna override it to
		if( g_KitParser.vecWeaponInfo.count( definition_override ) > 0 ) {
			const auto replacement_item = &g_KitParser.vecWeaponInfo.at( definition_override );

			const auto old_definition_index = definition_index;

			item.m_iItemDefinitionIndex( ) = definition_override;

			// Set the weapon model index -- required for paint kits to work on replacement items after the 29/11/2016 update.
			auto idx = g_pModelInfo->GetModelIndex( replacement_item->model );
			attribute->SetModelIndex( idx );

			auto networkable = attribute->GetClientNetworkable( );
			if( networkable ) {
				networkable->PreDataUpdate( 0 );
			}

			// We didn't override 0, but some actual weapon, that we have data for
			if( old_definition_index ) {
				if( g_KitParser.vecWeaponInfo.count( old_definition_index ) > 0 ) {
					const auto original_item = &g_KitParser.vecWeaponInfo.at( old_definition_index );
					if( original_item->icon && replacement_item->icon )
						icon_override_map[ original_item->icon ] = replacement_item->icon;
				}
			}
		}
	}
	else {
		EraseOverrideIfExistsByIndex( definition_index );
	}

	config->m_executed = false;
}

void SkinChanger::GloveChanger( C_CSPlayer *local ) {
	if( !local )
		return;

	const auto local_index = local->EntIndex( );

	player_info_t player_info;
	if( !g_pEngine->GetPlayerInfo( local_index, &player_info ) )
		return;

	auto &global = g_Vars.m_global_skin_changer;

	// Handle glove config
	{
		const auto wearables = local->m_hMyWearables( );

		const auto glove_config = GetDataFromIndex( global.m_gloves_idx );

		if( !glove_config || global.m_gloves_idx == 0 || !wearables ) {
			return;
		}

		auto wearable = ( C_BaseAttributableItem * )wearables[ 0 ].Get( );

		if( !wearable ) {
			// Try to get our last created glove
			const auto our_glove = ( C_BaseAttributableItem * )glove_handle.Get( );
			if( our_glove ) // Our glove still exists
			{
				wearables[ 0 ] = glove_handle;
				wearable = our_glove;
			}
		}

		if( local->IsDead( ) ) {
			// We are dead but we have gloves, destroy it
			if( wearable ) {
				auto networkable = wearable->GetClientNetworkable( );
				if( networkable ) {
					networkable->SetDestroyedOnRecreateEntities( );
					networkable->Release( );
				}
			}

			return;
		}

		// We don't have a glove, but we should
		bool just_created = false;
		if( !wearable ) {
			auto get_wearable_create_fn = [ ] ( ) -> CreateClientClassFn {
				auto clazz = g_pClient->GetAllClasses( );
				while( clazz->m_ClassID != CEconWearable )
					clazz = clazz->m_pNext;

				return ( CreateClientClassFn )clazz->m_pCreateFn;
			};

			static auto create_wearable_fn = get_wearable_create_fn( );

			const auto entry = g_pEntityList->GetHighestEntityIndex( ) + 1;
			const auto serial = rand( ) % 0x1000;
			create_wearable_fn( entry, serial );

			wearable = static_cast< C_BaseAttributableItem * >( g_pEntityList->GetClientEntity( entry ) );

			Vector new_pos = Vector{ 10000.0f, 10000.0f, 10000.f };
			wearable->SetAbsOrigin( new_pos );

			wearables[ 0 ] = CBaseHandle( entry | ( serial << 16 ) );

			// Let's store it in case we somehow lose it.
			glove_handle = wearables[ 0 ];

			just_created = true;

			if( wearable ) {
				//static auto fnEquip
				//	= reinterpret_cast< int( __thiscall* )( void*, void* ) >(
				//		Memory::Scan( XorStr( "client.dll" ), XorStr( "55 8B EC 83 EC 10 53 8B 5D 08 57 8B F9" ) )
				//		);

				//static auto fnInitializeAttributes
				//	= reinterpret_cast< int( __thiscall* )( void* ) >(
				//		Memory::Scan( XorStr( "client.dll" ), XorStr( "55 8B EC 83 E4 F8 83 EC 0C 53 56 8B F1 8B 86" ) )
				//		);

				//glove_config->m_executed = true;

				//auto config = glove_config;

				//const auto replacement_item = &g_KitParser.vecWeaponInfo.at( ( int )glove_config->m_definition_index );

				//wearable->m_Item( ).m_iItemIDHigh( ) = -1;
				//wearable->m_Item( ).m_iItemDefinitionIndex( ) = ( int )glove_config->m_definition_index;
				//wearable->m_Item( ).m_nFallbackPaintKit( ) = config->m_filter_paint_kits ? config->m_paint_kit : g_KitParser.vecPaintKits[ config->m_paint_kit_no_filter ].id;
				//wearable->m_Item( ).m_iEntityQuality( ) = 4;
				//wearable->m_Item( ).m_iAccountID( ) = player_info.xuid_low;
				//wearable->m_Item( ).m_bInitialized( ) = true;

				////printf( "%s\n", replacement_item->world_model );
				//wearable->SetModelIndex( g_pModelInfo->GetModelIndex( replacement_item->world_model ) );

				//fnEquip( wearable, local );
				//*( int* )( ( uintptr_t )local + 0xA20 ) = 1;
				//fnInitializeAttributes( wearable );

				//if( wearable->GetClientRenderable( ) ) {
				//	g_pClientLeafSystem->CreateRenderableHandle( wearable->GetClientRenderable( ), false, RENDERABLE_IS_OPAQUE, -1 );
				//}
			}

			glove_config->m_executed = true;
			if( wearable ) {
				*( int * )( ( uintptr_t )wearable + 0x64 ) = -1;
				ApplyConfigOnAttributableItem( wearable, glove_config, player_info.xuid_low );
			}
		}
	}
}
void SkinChanger::SequenceProxyFn( const CRecvProxyData *proxy_data_const, void *entity, void *output ) {
	if( g_SkinChanger.m_sequence_hook ) {
		static auto original_fn = g_SkinChanger.m_sequence_hook->GetOriginalFunction( );

		// Remove the constness from the proxy data allowing us to make changes.
		const auto proxy_data = const_cast< CRecvProxyData * >( proxy_data_const );

		const auto view_model = static_cast< C_BaseViewModel * >( entity );

		g_SkinChanger.DoSequenceRemapping( proxy_data, view_model );

		// Call the original function with our edited data.
		original_fn( proxy_data_const, entity, output );
	}
}

void SkinChanger::DoSequenceRemapping( CRecvProxyData *data, C_BaseViewModel *entity ) {
	auto local = C_CSPlayer::GetLocalPlayer( );
	if( !local || local->IsDead( ) )
		return;

	const auto owner = entity->m_hOwner( ).Get( );
	if( owner != local )
		return;

	const auto view_model_weapon = entity->m_hWeapon( ).Get( );
	if( !view_model_weapon )
		return;

	auto idx = view_model_weapon->m_Item( ).m_iItemDefinitionIndex( );
	if( g_KitParser.vecWeaponInfo.count( idx ) <= 0 )
		return;

	if( !g_Vars.m_global_skin_changer.m_knife_changer )
		return;

	const auto weapon_info = &g_KitParser.vecWeaponInfo.at( idx );

	if( weapon_info ) {
		const auto override_model = weapon_info->model;

		auto &sequence = data->m_Value.m_Int;
		sequence = GetNewAnimation( hash_32_fnv1a_const( override_model ), sequence, entity );
	}
}

int SkinChanger::GetNewAnimation( const uint32_t model, const int sequence, C_BaseViewModel *viewModel ) {

	// This only fixes if the original knife was a default knife.
	// The best would be having a function that converts original knife's sequence
	// into some generic enum, then another function that generates a sequence
	// from the sequences of the new knife. I won't write that.
	enum ESequence {
		SEQUENCE_DEFAULT_DRAW = 0,
		SEQUENCE_DEFAULT_IDLE1 = 1,
		SEQUENCE_DEFAULT_IDLE2 = 2,
		SEQUENCE_DEFAULT_LIGHT_MISS1 = 3,
		SEQUENCE_DEFAULT_LIGHT_MISS2 = 4,
		SEQUENCE_DEFAULT_HEAVY_MISS1 = 9,
		SEQUENCE_DEFAULT_HEAVY_HIT1 = 10,
		SEQUENCE_DEFAULT_HEAVY_BACKSTAB = 11,
		SEQUENCE_DEFAULT_LOOKAT01 = 12,

		SEQUENCE_BUTTERFLY_DRAW = 0,
		SEQUENCE_BUTTERFLY_DRAW2 = 1,
		SEQUENCE_BUTTERFLY_LOOKAT01 = 13,
		SEQUENCE_BUTTERFLY_LOOKAT03 = 15,

		SEQUENCE_FALCHION_IDLE1 = 1,
		SEQUENCE_FALCHION_HEAVY_MISS1 = 8,
		SEQUENCE_FALCHION_HEAVY_MISS1_NOFLIP = 9,
		SEQUENCE_FALCHION_LOOKAT01 = 12,
		SEQUENCE_FALCHION_LOOKAT02 = 13,

		SEQUENCE_DAGGERS_IDLE1 = 1,
		SEQUENCE_DAGGERS_LIGHT_MISS1 = 2,
		SEQUENCE_DAGGERS_LIGHT_MISS5 = 6,
		SEQUENCE_DAGGERS_HEAVY_MISS2 = 11,
		SEQUENCE_DAGGERS_HEAVY_MISS1 = 12,

		SEQUENCE_BOWIE_IDLE1 = 1,
	};

	auto random_sequence = [ ] ( const int low, const int high ) -> int {
		return rand( ) % ( high - low + 1 ) + low;
	};

	// Hashes for best performance.
	switch( model ) {
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_butterfly.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_DRAW:
					return random_sequence( SEQUENCE_BUTTERFLY_DRAW, SEQUENCE_BUTTERFLY_DRAW2 );
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( SEQUENCE_BUTTERFLY_LOOKAT01, SEQUENCE_BUTTERFLY_LOOKAT03 );
				default:
					return sequence + 1;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_falchion_advanced.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_IDLE2:
					return SEQUENCE_FALCHION_IDLE1;
				case SEQUENCE_DEFAULT_HEAVY_MISS1:
					return random_sequence( SEQUENCE_FALCHION_HEAVY_MISS1, SEQUENCE_FALCHION_HEAVY_MISS1_NOFLIP );
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( SEQUENCE_FALCHION_LOOKAT01, SEQUENCE_FALCHION_LOOKAT02 );
				case SEQUENCE_DEFAULT_DRAW:
				case SEQUENCE_DEFAULT_IDLE1:
					return sequence;
				default:
					return sequence - 1;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_push.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_IDLE2:
					return SEQUENCE_DAGGERS_IDLE1;
				case SEQUENCE_DEFAULT_LIGHT_MISS1:
				case SEQUENCE_DEFAULT_LIGHT_MISS2:
					return random_sequence( SEQUENCE_DAGGERS_LIGHT_MISS1, SEQUENCE_DAGGERS_LIGHT_MISS5 );
				case SEQUENCE_DEFAULT_HEAVY_MISS1:
					return random_sequence( SEQUENCE_DAGGERS_HEAVY_MISS2, SEQUENCE_DAGGERS_HEAVY_MISS1 );
				case SEQUENCE_DEFAULT_HEAVY_HIT1:
				case SEQUENCE_DEFAULT_HEAVY_BACKSTAB:
				case SEQUENCE_DEFAULT_LOOKAT01:
					return sequence + 3;
				case SEQUENCE_DEFAULT_DRAW:
				case SEQUENCE_DEFAULT_IDLE1:
					return sequence;
				default:
					return sequence + 2;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_survival_bowie.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_DRAW:
				case SEQUENCE_DEFAULT_IDLE1:
					return sequence;
				case SEQUENCE_DEFAULT_IDLE2:
					return SEQUENCE_BOWIE_IDLE1;
				default:
					return sequence - 1;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_ursus.mdl" ) ):
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_skeleton.mdl" ) ):
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_outdoor.mdl" ) ):
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_canis.mdl" ) ):
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_cord.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_DRAW:
					return random_sequence( SEQUENCE_BUTTERFLY_DRAW, SEQUENCE_BUTTERFLY_DRAW2 );
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( SEQUENCE_BUTTERFLY_LOOKAT01, 14 );
				default:
					return sequence + 1;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_stiletto.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( 12, 13 );
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_widowmaker.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( 14, 15 );
			}
		}

		default:
			return sequence;
	}

	return sequence;
}

CVariables::skin_changer_data *SkinChanger::GetDataFromIndex( int idx ) {
	auto &skin_data = g_Vars.m_skin_changer;
	for( size_t i = 0u; i < skin_data.Size( ); ++i ) {
		auto skin = skin_data[ i ];
		if( skin->m_definition_index == idx )
			return skin;
	}
	return nullptr;
}

void SkinChanger::ForceItemUpdate( C_CSPlayer *local ) {
	if( !local || local->IsDead( ) )
		return;

	auto ForceUpdate = [ ] ( C_BaseCombatWeapon *item ) {
		C_EconItemView *view = &item->m_Item( );

		if( !view )
			return;

		if( !item->GetClientNetworkable( ) )
			return;

		auto clearRefCountedVector = [ ] ( CUtlVector< IRefCounted * > &vec ) {
			for( int i = 0; i < vec.m_Size; ++i ) {
				auto &elem = vec.m_Memory.m_pMemory[ i ];
				if( elem ) {
					elem->unreference( );
					elem = nullptr;
				}
			}
			vec.m_Size = 0;
		};

		auto clearCustomMaterials = [ ] ( CUtlVector< IRefCounted * > &vec ) {
			for( int i = 0; i < vec.m_Size; ++i ) {
				auto &element = vec.m_Memory.m_pMemory[ i ];
				// actually makes no sense
				*( int * )( ( ( uintptr_t )element ) + 0x10 ) = 0;
				*( int * )( ( ( uintptr_t )element ) + 0x18 ) = 0;
				*( int * )( ( ( uintptr_t )element ) + 0x20 ) = 0;
				*( int * )( ( ( uintptr_t )element ) + 0x24 ) = 0;
				vec.m_Memory.m_pMemory[ i ] = nullptr;
			}

			vec.m_Size = 0;
		};

		item->m_bCustomMaterialInitialized( ) = false;
		clearCustomMaterials( item->m_CustomMaterials( ) );
		clearCustomMaterials( view->m_CustomMaterials( ) );
		clearRefCountedVector( view->m_VisualsDataProcessors( ) );

		item->GetClientNetworkable( )->PostDataUpdate( 0 );
		item->GetClientNetworkable( )->OnDataChanged( 0 );
	};

	auto &global = g_Vars.m_global_skin_changer;
	auto weapons = local->m_hMyWeapons( );
	for( size_t i = 0; i < 48; ++i ) {
		auto weaponHandle = weapons[ i ];
		if( !weaponHandle.IsValid( ) )
			break;

		auto pWeapon = ( C_WeaponCSBaseGun * )weaponHandle.Get( );
		if( !pWeapon )
			continue;

		ForceUpdate( pWeapon );
	}

	UpdateHud( );
}

void SkinChanger::UpdateHud( ) {

}
