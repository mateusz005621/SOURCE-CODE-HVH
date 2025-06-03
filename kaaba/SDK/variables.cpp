#include "variables.hpp"
#include "../source.hpp"
#include "../Features/Miscellaneous/KitParser.hpp"
#include "color.hpp"
std::vector<hotkey_t*> g_keybinds;

CVariables g_Vars;

Color_f Color_f::Black = Color_f( 0.0f, 0.0f, 0.0f, 1.0f );
Color_f Color_f::White = Color_f( 1.0f, 1.0f, 1.0f, 1.0f );
Color_f Color_f::Gray = Color_f( 0.75f, 0.75f, 0.75f, 1.0f );

Color_f Color_f::Lerp( const Color_f& dst, float t ) const {
   return Color_f( ( dst.r - r ) * t + r, ( dst.g - g ) * t + g, ( dst.b - b ) * t + b, ( dst.a - a ) * t + a );
}

void Color_f::SetColor( Color clr ) {
    r = static_cast< float >( clr.r( ) ) / 255.0f;
    g = static_cast< float >( clr.g( ) ) / 255.0f;
    b = static_cast< float >( clr.b( ) ) / 255.0f;
    a = static_cast< float >( clr.a( ) ) / 255.0f;
}

Color Color_f::ToRegularColor( ) {
    return Color( r * 255.f, g * 255.f, b * 255.f, a * 255.f );
}

void CVariables::Create( ) {
   // add weapons
   m_skin_changer.SetName( XorStr( "skin_changer" ) );
   for ( const auto& value : g_KitParser.vecWeapons ) {
	  auto idx = m_skin_changer.AddEntry( );
	  auto entry = m_skin_changer[ idx ];
	  entry->m_definition_index = value.id;
	  entry->SetName( value.name );
   }

   this->AddChild( &m_skin_changer );

   m_loaded_luas.SetName( XorStr( "lua_scripts" ) );
   this->AddChild( &m_loaded_luas );

   sv_accelerate = g_pCVar->FindVar( XorStr( "sv_accelerate" ) );
   sv_airaccelerate = g_pCVar->FindVar( XorStr( "sv_airaccelerate" ) );
   sv_gravity = g_pCVar->FindVar( XorStr( "sv_gravity" ) );
   sv_jump_impulse = g_pCVar->FindVar( XorStr( "sv_jump_impulse" ) );
   sv_penetration_type = g_pCVar->FindVar( XorStr( "sv_penetration_type" ) );

   m_yaw = g_pCVar->FindVar( XorStr( "m_yaw" ) );
   m_pitch = g_pCVar->FindVar( XorStr( "m_pitch" ) );
   sensitivity = g_pCVar->FindVar( XorStr( "sensitivity" ) );

   cl_sidespeed = g_pCVar->FindVar( XorStr( "cl_sidespeed" ) );
   cl_forwardspeed = g_pCVar->FindVar( XorStr( "cl_forwardspeed" ) );
   cl_upspeed = g_pCVar->FindVar( XorStr( "cl_upspeed" ) );
   cl_extrapolate = g_pCVar->FindVar( XorStr( "cl_extrapolate" ) );

   sv_noclipspeed = g_pCVar->FindVar( XorStr( "sv_noclipspeed" ) );

   weapon_recoil_scale = g_pCVar->FindVar( XorStr( "weapon_recoil_scale" ) );
   view_recoil_tracking = g_pCVar->FindVar( XorStr( "view_recoil_tracking" ) );

   r_jiggle_bones = g_pCVar->FindVar( XorStr( "r_jiggle_bones" ) );

   mp_friendlyfire = g_pCVar->FindVar( XorStr( "mp_friendlyfire" ) );

   sv_maxunlag = g_pCVar->FindVar( XorStr( "sv_maxunlag" ) );
   sv_minupdaterate = g_pCVar->FindVar( XorStr( "sv_minupdaterate" ) );
   sv_maxupdaterate = g_pCVar->FindVar( XorStr( "sv_maxupdaterate" ) );

   sv_client_min_interp_ratio = g_pCVar->FindVar( XorStr( "sv_client_min_interp_ratio" ) );
   sv_client_max_interp_ratio = g_pCVar->FindVar( XorStr( "sv_client_max_interp_ratio" ) );

   cl_interp_ratio = g_pCVar->FindVar( XorStr( "cl_interp_ratio" ) );
   cl_interp = g_pCVar->FindVar( XorStr( "cl_interp" ) );
   cl_updaterate = g_pCVar->FindVar( XorStr( "cl_updaterate" ) );

   game_type = g_pCVar->FindVar( XorStr( "game_type" ) );
   game_mode = g_pCVar->FindVar( XorStr( "game_mode" ) );

   ff_damage_bullet_penetration = g_pCVar->FindVar( XorStr( "ff_damage_bullet_penetration" ) );
   ff_damage_reduction_bullets = g_pCVar->FindVar( XorStr( "ff_damage_reduction_bullets" ) );

   mp_c4timer = g_pCVar->FindVar( XorStr( "mp_c4timer" ) );

   mp_damage_scale_ct_head = g_pCVar->FindVar( XorStr( "mp_damage_scale_ct_head" ) );
   mp_damage_scale_t_head = g_pCVar->FindVar( XorStr( "mp_damage_scale_t_head" ) );
   mp_damage_scale_ct_body = g_pCVar->FindVar( XorStr( "mp_damage_scale_ct_body" ) );
   mp_damage_scale_t_body = g_pCVar->FindVar( XorStr( "mp_damage_scale_t_body" ) );

   viewmodel_fov = g_pCVar->FindVar( XorStr( "viewmodel_fov" ) );
   viewmodel_offset_x = g_pCVar->FindVar( XorStr( "viewmodel_offset_x" ) );
   viewmodel_offset_y = g_pCVar->FindVar( XorStr( "viewmodel_offset_y" ) );
   viewmodel_offset_z = g_pCVar->FindVar( XorStr( "viewmodel_offset_z" ) );

   mat_ambient_light_r = g_pCVar->FindVar( XorStr( "mat_ambient_light_r" ) );
   mat_ambient_light_g = g_pCVar->FindVar( XorStr( "mat_ambient_light_g" ) );
   mat_ambient_light_b = g_pCVar->FindVar( XorStr( "mat_ambient_light_b" ) );

   sv_show_impacts = g_pCVar->FindVar( XorStr( "sv_showimpacts" ) );

   molotov_throw_detonate_time = g_pCVar->FindVar( XorStr( "molotov_throw_detonate_time" ) );
   weapon_molotov_maxdetonateslope = g_pCVar->FindVar( XorStr( "weapon_molotov_maxdetonateslope" ) );
   net_client_steamdatagram_enable_override = g_pCVar->FindVar( XorStr( "net_client_steamdatagram_enable_override" ) );
   mm_dedicated_search_maxping = g_pCVar->FindVar( XorStr( "mm_dedicated_search_maxping" ) );

   cl_csm_shadows = g_pCVar->FindVar( XorStr( "cl_csm_shadows" ) );

   r_drawmodelstatsoverlay = g_pCVar->FindVar(XorStr("r_drawmodelstatsoverlay"));
   host_limitlocal = g_pCVar->FindVar(XorStr("host_limitlocal"));

   sv_clockcorrection_msecs = g_pCVar->FindVar( XorStr( "sv_clockcorrection_msecs" ) );
   sv_max_usercmd_future_ticks = g_pCVar->FindVar( XorStr( "sv_max_usercmd_future_ticks" ) );
   sv_maxusrcmdprocessticks = g_pCVar->FindVar( XorStr( "sv_maxusrcmdprocessticks" ) );
   crosshair = g_pCVar->FindVar( XorStr( "crosshair" ) );
   engine_no_focus_sleep = g_pCVar->FindVar( XorStr( "engine_no_focus_sleep" ) );

   r_3dsky = g_pCVar->FindVar( XorStr( "r_3dsky" ) );
   r_RainRadius = g_pCVar->FindVar( XorStr( "r_RainRadius" ) );
   r_rainalpha = g_pCVar->FindVar( XorStr( "r_rainalpha" ) );
   cl_crosshair_sniper_width = g_pCVar->FindVar( XorStr( "cl_crosshair_sniper_width" ) );

   sv_friction = g_pCVar->FindVar( XorStr( "sv_friction" ) );
   sv_stopspeed = g_pCVar->FindVar( XorStr( "sv_stopspeed" ) );

   zoom_sensitivity_ratio_mouse = g_pCVar->FindVar( XorStr( "zoom_sensitivity_ratio_mouse" ) );

   name = g_pCVar->FindVar( XorStr( "name" ) );
   voice_loopback = g_pCVar->FindVar( XorStr( "voice_loopback" ) );

   /*developer = g_pCVar->FindVar( XorStr( "developer" ) );
   con_enable = g_pCVar->FindVar( XorStr( "con_enable" ) );
   con_filter_enable = g_pCVar->FindVar( XorStr( "con_filter_enable" ) );
   con_filter_text = g_pCVar->FindVar( XorStr( "con_filter_text" ) );
   con_filter_text_out = g_pCVar->FindVar( XorStr( "con_filter_text_out" ) );
   contimes = g_pCVar->FindVar( XorStr( "contimes" ) );*/
}