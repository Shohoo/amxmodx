/*
 * DoDX 
 * Copyright (c) 2004 Lukasz Wlasinski
 *
 *
 *    This program is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published by the
 *    Free Software Foundation; either version 2 of the License, or (at
 *    your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful, but
 *    WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software Foundation,
 *    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    In addition, as a special exception, the author gives permission to
 *    link the code of this program with the Half-Life Game Engine ("HL
 *    Engine") and Modified Game Libraries ("MODs") developed by Valve,
 *    L.L.C ("Valve").  You must obey the GNU General Public License in all
 *    respects for all of the code used other than the HL Engine and MODs
 *    from Valve.  If you modify this file, you may extend this exception
 *    to your version of the file, but you are not obligated to do so.  If
 *    you do not wish to do so, delete this exception statement from your
 *    version.
 *
 */

#include "amxxmodule.h"
#include "dodx.h"

funEventCall modMsgsEnd[MAX_REG_MSGS];
funEventCall modMsgs[MAX_REG_MSGS];
void (*function)(void*);
void (*endfunction)(void*);
CPlayer* mPlayer;
CPlayer players[33];
CMapInfo g_map;

bool rankBots;
int mState;
int mPlayerIndex;

int AlliesScore;
int AxisScore;

#ifdef FORWARD_OLD_SYSTEM

Forward g_death_info;
Forward g_damage_info;

#else

int iFDamage;
int iFDeath;

#endif

int gmsgCurWeapon;
int gmsgHealth;
int gmsgResetHUD;
int gmsgObjScore;
int gmsgRoundState;

int gmsgTeamScore;
int gmsgScoreShort;
int gmsgPTeam;

int gmsgAmmoX;
int gmsgAmmoShort;

RankSystem g_rank;
Grenades g_grenades;


cvar_t init_dodstats_maxsize ={"dodstats_maxsize","3500", 0 , 3500.0 };
cvar_t init_dodstats_reset ={"dodstats_reset","0"};
cvar_t init_dodstats_rank ={"dodstats_rank","0"};
cvar_t init_dodstats_rankbots ={"dodstats_rankbots","1"};
cvar_t init_dodstats_pause = {"dodstats_pause","0"};
cvar_t *dodstats_maxsize;
cvar_t *dodstats_reset;
cvar_t *dodstats_rank;
cvar_t *dodstats_rankbots;
cvar_t *dodstats_pause;

struct sUserMsg {
	const char *name;
	int* id;
	funEventCall func;
	bool endmsg;
} g_user_msg[] = {
	{ "CurWeapon",&gmsgCurWeapon,Client_CurWeapon,false },
	{ "ObjScore",&gmsgObjScore,Client_ObjScore,false },
	{ "RoundState",&gmsgRoundState,Client_RoundState,false },
	{ "Health",&gmsgHealth,Client_Health_End,true },
	{ "ResetHUD",&gmsgResetHUD,Client_ResetHUD_End,true },

	{ "TeamScore",&gmsgTeamScore,Client_TeamScore,false },
	{ "ScoreShort",&gmsgScoreShort,NULL,false },
	{ "PTeam",&gmsgPTeam,NULL,false },

	{ "AmmoX",&gmsgAmmoX,Client_AmmoX,false},
	{ "AmmoShort",&gmsgAmmoShort,Client_AmmoShort,false},

	{ 0,0,0,false }
};

const char* get_localinfo( const char* name , const char* def = 0 )
{
	const char* b = LOCALINFO( (char*)name );
	if (((b==0)||(*b==0)) && def )
		SET_LOCALINFO((char*)name,(char*)(b = def) );
	return b;
}

int RegUserMsg_Post(const char *pszName, int iSize){
	for (int i = 0; g_user_msg[ i ].name; ++i ){
		if ( !*g_user_msg[i].id && strcmp( g_user_msg[ i ].name , pszName  ) == 0 ){
			int id = META_RESULT_ORIG_RET( int );

			*g_user_msg[ i ].id = id;
		
			if ( g_user_msg[ i ].endmsg )
				modMsgsEnd[ id  ] = g_user_msg[ i ].func;
			else
				modMsgs[ id  ] = g_user_msg[ i ].func;

			break;
		}
	}

	RETURN_META_VALUE(MRES_IGNORED, 0);
}

void ServerActivate_Post( edict_t *pEdictList, int edictCount, int clientMax ){
	
	rankBots = (int)dodstats_rankbots->value ? true:false;

	for( int i = 1; i <= gpGlobals->maxClients; ++i )
		GET_PLAYER_POINTER_I(i)->Init( i , pEdictList + i );


	RETURN_META(MRES_IGNORED);
}

void PlayerPreThink_Post( edict_t *pEntity ) {
	if ( !isModuleActive() ) 
		return;

	CPlayer *pPlayer = GET_PLAYER_POINTER(pEntity);

	if (pPlayer->clearStats && pPlayer->clearStats < gpGlobals->time && pPlayer->ingame){

	if ( !ignoreBots(pEntity) ){
		pPlayer->clearStats = 0.0f;
		pPlayer->rank->updatePosition( &pPlayer->life );
		pPlayer->restartStats(false);
	}

	}
	RETURN_META(MRES_IGNORED);
}

void ServerDeactivate() {
	int i;
	for( i = 1;i<=gpGlobals->maxClients; ++i){
		CPlayer *pPlayer = GET_PLAYER_POINTER_I(i);
		if (pPlayer->ingame) pPlayer->Disconnect();
	}

	if ( (g_rank.getRankNum() >= (int)dodstats_maxsize->value) || ((int)dodstats_reset->value == 1) ) {
		CVAR_SET_FLOAT("dodstats_reset",0.0);
		g_rank.clear();
	}
	
	g_rank.saveRank( MF_BuildPathname("%s",get_localinfo("dodstats") ) );

#ifdef FORWARD_OLD_SYSTEM

	g_damage_info.clear();
	g_death_info.clear();

#endif

	// clear custom weapons info
	for ( i=DODMAX_WEAPONS-DODMAX_CUSTOMWPNS;i<DODMAX_WEAPONS;i++)
		weaponData[i].needcheck = false;

	RETURN_META(MRES_IGNORED);
}

BOOL ClientConnect_Post( edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[ 128 ]  ){
	GET_PLAYER_POINTER(pEntity)->Connect(pszName,pszAddress);
	
	RETURN_META_VALUE(MRES_IGNORED, TRUE);
}

void ClientDisconnect( edict_t *pEntity ) {
	CPlayer *pPlayer = GET_PLAYER_POINTER(pEntity);
		
	if (pPlayer->ingame)
		pPlayer->Disconnect();

	RETURN_META(MRES_IGNORED);
}

void ClientPutInServer_Post( edict_t *pEntity ) {
	GET_PLAYER_POINTER(pEntity)->PutInServer();

	RETURN_META(MRES_IGNORED);
}

void ClientUserInfoChanged_Post( edict_t *pEntity, char *infobuffer ) {
	CPlayer *pPlayer = GET_PLAYER_POINTER(pEntity);

	const char* name = INFOKEY_VALUE(infobuffer,"name");
	const char* oldname = STRING(pEntity->v.netname);

	if ( pPlayer->ingame){
		if ( strcmp(oldname,name) ) {
			if (!dodstats_rank->value)
				pPlayer->rank = g_rank.findEntryInRank( name, name );
			else
				pPlayer->rank->setName( name );
		}
	}
	else if ( pPlayer->IsBot() ) {
		pPlayer->Connect( name , "127.0.0.1" );
		pPlayer->PutInServer();
	}

	RETURN_META(MRES_IGNORED);
}

void MessageBegin_Post(int msg_dest, int msg_type, const float *pOrigin, edict_t *ed) {
	if (ed){
		mPlayerIndex = ENTINDEX(ed);
		mPlayer = GET_PLAYER_POINTER_I(mPlayerIndex);
	} else {
		mPlayerIndex = 0;
		mPlayer = NULL;
	}
	mState = 0;
	if ( msg_type < 0 || msg_type >= MAX_REG_MSGS )
		msg_type = 0;
	function=modMsgs[msg_type];
	endfunction=modMsgsEnd[msg_type];
	RETURN_META(MRES_IGNORED);
}

void MessageEnd_Post(void) {
	if (endfunction) (*endfunction)(NULL);
	RETURN_META(MRES_IGNORED);
}

void WriteByte_Post(int iValue) {
	if (function) (*function)((void *)&iValue);
	RETURN_META(MRES_IGNORED);
}

void WriteChar_Post(int iValue) {
	if (function) (*function)((void *)&iValue);
	RETURN_META(MRES_IGNORED);
}

void WriteShort_Post(int iValue) {
	if (function) (*function)((void *)&iValue);
	RETURN_META(MRES_IGNORED);
}

void WriteLong_Post(int iValue) {
	if (function) (*function)((void *)&iValue);
	RETURN_META(MRES_IGNORED);
}

void WriteAngle_Post(float flValue) {
	if (function) (*function)((void *)&flValue);
	RETURN_META(MRES_IGNORED);
}

void WriteCoord_Post(float flValue) {
	if (function) (*function)((void *)&flValue);
	RETURN_META(MRES_IGNORED);
}

void WriteString_Post(const char *sz) {
	if (function) (*function)((void *)sz);
	RETURN_META(MRES_IGNORED);
}

void WriteEntity_Post(int iValue) {
	if (function) (*function)((void *)&iValue);
	RETURN_META(MRES_IGNORED);
}

void TraceLine_Post(const float *v1, const float *v2, int fNoMonsters, edict_t *e, TraceResult *ptr) {
	if (ptr->pHit&&(ptr->pHit->v.flags& (FL_CLIENT | FL_FAKECLIENT) )&&
		e && (e->v.flags&(FL_CLIENT | FL_FAKECLIENT) )){
		GET_PLAYER_POINTER(e)->aiming = ptr->iHitgroup;
		RETURN_META(MRES_IGNORED);
	}

	if ( e && e->v.owner && e->v.owner->v.flags& (FL_CLIENT | FL_FAKECLIENT) ){
		CPlayer *pPlayer = GET_PLAYER_POINTER(e->v.owner);
		for ( int i=0;i<MAX_TRACE;i++){
			if ( strcmp( traceData[i].szName,STRING(e->v.classname)) == 0 ){
				if ( traceData[i].iAction & ACT_NADE_SHOT  ){
					if ( traceData[i].iId == 13 && g_map.detect_allies_country )
						pPlayer->saveShot(36);
					else
						pPlayer->saveShot(traceData[i].iId);
				}
				if ( traceData[i].iAction & ACT_NADE_PUT ){
					g_grenades.put(e,traceData[i].fDel, (traceData[i].iId == 13 && g_map.detect_allies_country )?36:traceData[i].iId ,GET_PLAYER_POINTER(e->v.owner));
				}
				break;
			}
		}
	}
	RETURN_META(MRES_IGNORED);
}

void DispatchKeyValue_Post( edict_t *pentKeyvalue, KeyValueData *pkvd ){

	if ( !pkvd->szClassName ){ 
		// info_doddetect
		if ( pkvd->szValue[0]=='i' && pkvd->szValue[5]=='d' ){
			g_map.pEdict = pentKeyvalue;
			g_map.initialized = true;
		}
		RETURN_META(MRES_IGNORED);
	}
	// info_doddetect
	if ( g_map.initialized && pentKeyvalue == g_map.pEdict ){
		if ( pkvd->szKeyName[0]=='d' && pkvd->szKeyName[7]=='a' ){
			if ( pkvd->szKeyName[8]=='l' ){
				switch ( pkvd->szKeyName[14] ){
				case 'c': 
					g_map.detect_allies_country=atoi(pkvd->szValue); 
					break;
				case 'p': 
					g_map.detect_allies_paras=atoi(pkvd->szValue); 
					break;
				}
			} 
			else if ( pkvd->szKeyName[12]=='p' ) g_map.detect_axis_paras=atoi(pkvd->szValue); 
		}
	}
	RETURN_META(MRES_IGNORED);
}

void OnMetaAttach() {
	
	CVAR_REGISTER (&init_dodstats_maxsize);
	CVAR_REGISTER (&init_dodstats_reset);
	CVAR_REGISTER (&init_dodstats_rank);
	CVAR_REGISTER (&init_dodstats_rankbots);
	CVAR_REGISTER (&init_dodstats_pause);
	dodstats_maxsize=CVAR_GET_POINTER(init_dodstats_maxsize.name);
	dodstats_reset=CVAR_GET_POINTER(init_dodstats_reset.name);
	dodstats_rank=CVAR_GET_POINTER(init_dodstats_rank.name);
	dodstats_rankbots = CVAR_GET_POINTER(init_dodstats_rankbots.name);
	dodstats_pause = CVAR_GET_POINTER(init_dodstats_pause.name);
}

void OnAmxxAttach() {

	MF_AddNatives( stats_Natives );
	MF_AddNatives( base_Natives );

	const char* path =  get_localinfo("dodstats_score"/*,"addons/amxx/dodstats.amx"*/);
	if ( path && *path ) {
		char error[128];
		g_rank.loadCalc( MF_BuildPathname("%s",path) , error  );
	}
	
	if ( !g_rank.begin() ){		
		g_rank.loadRank( MF_BuildPathname("%s",
			get_localinfo("dodstats"/*,"addons/amxx/dodstats.dat"*/) ) );
	}

	g_map.Init();
	
}

void OnAmxxDetach() {
	g_rank.clear();
	g_grenades.clear();
	g_rank.unloadCalc();
}

#ifndef FORWARD_OLD_SYSTEM

void OnPluginsLoaded(){
	iFDeath = MF_RegisterForward("client_death",ET_IGNORE,FP_CELL,FP_CELL,FP_CELL,FP_CELL,FP_CELL,FP_DONE);
	iFDamage = MF_RegisterForward("client_damage",ET_IGNORE,FP_CELL,FP_CELL,FP_CELL,FP_CELL,FP_CELL,FP_CELL,FP_DONE);

}

#endif
