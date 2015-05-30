/*	EQEMu: Everquest Server Emulator
Copyright (C) 2001-2002 EQEMu Development Team (http://eqemu.org)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY except by those people which sell it, which
are required to give you total support for your newly bought product;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "client.h"
#include "entity.h"
#include "groups.h"
#include "mob.h"
#include "raids.h"

#include "../common/rulesys.h"

#include "hate_list.h"
#include "quest_parser_collection.h"
#include "zone.h"
#include "water_map.h"

#include <stdlib.h>
#include <list>

extern Zone *zone;

HateList::HateList()
{
	hate_owner = nullptr;
}

HateList::~HateList()
{
}

// added for frenzy support
// checks if target still is in frenzy mode
void HateList::IsEntityInFrenzyMode()
{
	auto iterator = list.begin();
	while (iterator != list.end())
	{
		if ((*iterator)->entity_on_hatelist->GetHPRatio() >= 20)
			(*iterator)->is_entity_frenzy = false;
		++iterator;
	}
}

void HateList::WipeHateList()
{
	auto iterator = list.begin();

	while (iterator != list.end())
	{
		Mob* m = (*iterator)->entity_on_hatelist;
		if (m)
		{
			parse->EventNPC(EVENT_HATE_LIST, hate_owner->CastToNPC(), m, "0", 0);

			if (m->IsClient())
				m->CastToClient()->DecrementAggroCount();
		}
		delete (*iterator);
		iterator = list.erase(iterator);

	}
}

bool HateList::IsEntOnHateList(Mob *mob)
{
	if (Find(mob))
		return true;
	return false;
}

struct_HateList *HateList::Find(Mob *in_entity)
{
	auto iterator = list.begin();
	while (iterator != list.end())
	{
		if ((*iterator)->entity_on_hatelist == in_entity)
			return (*iterator);
		++iterator;
	}
	return nullptr;
}

void HateList::SetHateAmountOnEnt(Mob* other, uint32 in_hate, uint32 in_damage)
{
	struct_HateList *entity = Find(other);
	if (entity)
	{
		if (in_damage > 0)
			entity->hatelist_damage = in_damage;
		if (in_hate > 0)
			entity->stored_hate_amount = in_hate;
	}
}

Mob* HateList::GetDamageTopOnHateList(Mob* hater, bool ignore_mezzed)
{
	Mob* current = nullptr;
	Group* grp = nullptr;
	Raid* r = nullptr;
	uint32 dmg_amt = 0;

	for (auto iterator = list.begin(); iterator != list.end(); ++iterator) {
		if (!(*iterator) || !(*iterator)->entity_on_hatelist)
			continue;

		Mob *hated = (*iterator)->entity_on_hatelist;

		if (ignore_mezzed && hated->IsMezzed())
			continue;

		if (hated->IsClient()) {
			r = entity_list.GetRaidByClient(hated->CastToClient());
		}
		else {
			r = nullptr;
		}

		grp = entity_list.GetGroupByMob(hated);

		if (r) {
			if (r->GetTotalRaidDamage(hater) >= dmg_amt) {
				current = hated;
				dmg_amt = r->GetTotalRaidDamage(hater);
			}
		}
		else if (grp) {
			if (grp->GetTotalGroupDamage(hater) >= dmg_amt) {
				current = hated;
				dmg_amt = grp->GetTotalGroupDamage(hater);
			}
		}
		else {
			if ((uint32)(*iterator)->hatelist_damage >= dmg_amt) {
				current = hated;
				dmg_amt = (*iterator)->hatelist_damage;
			}
		}
	}

	return current;
}

Mob* HateList::GetClosestEntOnHateList(Mob *hater, bool ignore_mezzed)
{
	Mob* close_entity = nullptr;
	float close_distance = 99999.9f;
	float this_distance;

	for (auto iterator = list.begin(); iterator != list.end(); ++iterator) {
		if (!(*iterator) || !(*iterator)->entity_on_hatelist)
			continue;

		Mob *hated = (*iterator)->entity_on_hatelist;

		if (ignore_mezzed && hated->IsMezzed())
			continue;

		this_distance = DistanceSquaredNoZ(hated->GetPosition(), hater->GetPosition());
		if (this_distance <= close_distance) {
			close_distance = this_distance;
			close_entity = hated;
		}
	}

	if ((!close_entity && hater->IsNPC()) || (close_entity && close_entity->DivineAura())) {
		close_entity = hater->CastToNPC()->GetHateTop();
	}

	return close_entity;
}

void HateList::AddEntToHateList(Mob *in_entity, int32 in_hate, int32 in_damage, bool in_is_entity_frenzied, bool iAddIfNotExist)
{
	if (!in_entity)
		return;

	if (in_entity->IsCorpse())
		return;

	if (in_entity->IsClient() && in_entity->CastToClient()->IsDead())
		return;

	struct_HateList *entity = Find(in_entity);
	if (entity)
	{
		entity->hatelist_damage += (in_damage >= 0) ? in_damage : 0;
		entity->stored_hate_amount += in_hate;
		entity->is_entity_frenzy = in_is_entity_frenzied;
	}
	else if (iAddIfNotExist) {
		entity = new struct_HateList;
		entity->entity_on_hatelist = in_entity;
		entity->hatelist_damage = (in_damage >= 0) ? in_damage : 0;
		entity->stored_hate_amount = in_hate;
		entity->is_entity_frenzy = in_is_entity_frenzied;
		list.push_back(entity);
		parse->EventNPC(EVENT_HATE_LIST, hate_owner->CastToNPC(), in_entity, "1", 0);

		if (in_entity->IsClient()) {
			if (hate_owner->CastToNPC()->IsRaidTarget())
				in_entity->CastToClient()->SetEngagedRaidTarget(true);
			in_entity->CastToClient()->IncrementAggroCount();
		}
	}
}

bool HateList::RemoveEntFromHateList(Mob *in_entity)
{
	if (!in_entity)
		return false;

	bool is_found = false;
	auto iterator = list.begin();

	while (iterator != list.end())
	{
		if ((*iterator)->entity_on_hatelist == in_entity)
		{
			is_found = true;

			if (in_entity && in_entity->IsClient())
				in_entity->CastToClient()->DecrementAggroCount();

			delete (*iterator);
			iterator = list.erase(iterator);

			if (in_entity)
				parse->EventNPC(EVENT_HATE_LIST, hate_owner->CastToNPC(), in_entity, "0", 0);

		}
		else
			++iterator;
	}
	return is_found;
}

void HateList::DoFactionHits(int32 npc_faction_level_id) {
	if (npc_faction_level_id <= 0)
		return;
	auto iterator = list.begin();
	while (iterator != list.end())
	{
		Client *client;

		if ((*iterator)->entity_on_hatelist && (*iterator)->entity_on_hatelist->IsClient())
			client = (*iterator)->entity_on_hatelist->CastToClient();
		else
			client = nullptr;

		if (client)
			client->SetFactionLevel(client->CharacterID(), npc_faction_level_id, client->GetBaseClass(), client->GetBaseRace(), client->GetDeity());
		++iterator;
	}
}

int HateList::GetSummonedPetCountOnHateList(Mob *hater) {

	//Function to get number of 'Summoned' pets on a targets hate list to allow calculations for certian spell effects.
	//Unclear from description that pets are required to be 'summoned body type'. Will not require at this time.
	int pet_count = 0;
	auto iterator = list.begin();
	while (iterator != list.end()) {

		if ((*iterator)->entity_on_hatelist != nullptr && (*iterator)->entity_on_hatelist->IsNPC() && ((*iterator)->entity_on_hatelist->CastToNPC()->IsPet() || ((*iterator)->entity_on_hatelist->CastToNPC()->GetSwarmOwner() > 0)))
		{
			++pet_count;
		}

		++iterator;
	}

	return pet_count;
}

Mob *HateList::GetEntWithMostHateOnList(Mob *center, bool ignore_mezzed)
{
	// hack fix for zone shutdown crashes on some servers
	if (!zone->IsLoaded())
		return nullptr;

	if (center == nullptr)
		return nullptr;

	Mob* top_hate = nullptr;
	int64 hate = -1;

	if (RuleB(Aggro, SmartAggroList)) {
		Mob* top_client_type_in_range = nullptr;
		int64 hate_client_type_in_range = -1;
		int skipped_count = 0;

		for (auto iterator = list.begin(); iterator != list.end(); ++iterator) {
			if (!(*iterator) || !(*iterator)->entity_on_hatelist)
				continue;

			struct_HateList *cur = (*iterator);
			Mob *hated = cur->entity_on_hatelist;
			int16 aggro_mod = 0;

			if (ignore_mezzed && hated->IsMezzed())
				continue;

			auto hateEntryPosition = glm::vec3(hated->GetX(), hated->GetY(), hated->GetZ());
			if (center->IsNPC() && center->CastToNPC()->IsUnderwaterOnly() && zone->HasWaterMap()) {
				if (!zone->watermap->InLiquid(hateEntryPosition)) {
					skipped_count++;
					continue;
				}
			}

			if (hated->Sanctuary()) {
				if (hate == -1) {
					top_hate = hated;
					hate = 1;
				}

				continue;
			}

			if (hated->DivineAura() || hated->IsMezzed() || hated->IsFeared()) {
				if (hate == -1) {
					top_hate = hated;
					hate = 0;
				}

				continue;
			}

			int64 current_hate = cur->stored_hate_amount;

			if (hated->IsClient()) {
				if (hated->CastToClient()->IsSitting()) {
					aggro_mod += RuleI(Aggro, SittingAggroMod);
				}

				if (center) {
					if (center->GetTarget() == hated) {
						aggro_mod += RuleI(Aggro, CurrentTargetAggroMod);
					}

					if (RuleI(Aggro, MeleeRangeAggroMod) != 0) {
						if (center->CombatRange(hated)) {
							aggro_mod += RuleI(Aggro, MeleeRangeAggroMod);

							if (current_hate > hate_client_type_in_range || cur->is_entity_frenzy) {
								hate_client_type_in_range = current_hate;
								top_client_type_in_range = hated;
							}
						}
					}
				}
			}
			else {
				if (center) {
					if (center->GetTarget() == hated) {
						aggro_mod += RuleI(Aggro, CurrentTargetAggroMod);
					}

					if (RuleI(Aggro, MeleeRangeAggroMod) != 0) {
						if (center->CombatRange(hated)) {
							aggro_mod += RuleI(Aggro, MeleeRangeAggroMod);
						}
					}
				}
			}

			if (hated->GetMaxHP() != 0 && ((hated->GetHP() * 100 / hated->GetMaxHP()) < 20)) {
				aggro_mod += RuleI(Aggro, CriticallyWoundedAggroMod);
			}

			if (aggro_mod) {
				current_hate += (current_hate * aggro_mod / 100);
			}

			if (current_hate > hate || cur->is_entity_frenzy) {
				hate = current_hate;
				top_hate = hated;
			}

		}

		if (top_client_type_in_range != nullptr && top_hate != nullptr) {
			bool isTopClientType = top_hate->IsClient();

#ifdef BOTS
			if (!isTopClientType) {
				if (top_hate->IsBot()) {
					isTopClientType = true;
					top_client_type_in_range = top_hate;
				}
			}
#endif //BOTS

			if (!isTopClientType) {
				if (top_hate->IsMerc()) {
					isTopClientType = true;
					top_client_type_in_range = top_hate;
				}
			}

			if (!isTopClientType) {
				if (top_hate->GetSpecialAbility(ALLOW_TO_TANK)) {
					isTopClientType = true;
					top_client_type_in_range = top_hate;
				}
			}

			if (!isTopClientType)
				return top_client_type_in_range ? top_client_type_in_range : nullptr;

			return top_hate ? top_hate : nullptr;
		}
		else {
			if (top_hate == nullptr && skipped_count > 0) {
				if (center->GetTarget() && !(ignore_mezzed && center->GetTarget()->IsMezzed()))
					return center->GetTarget();

				return nullptr;
			}

			return top_hate ? top_hate : nullptr;
		}
	}
	else {
		int skipped_count = 0;
		for (auto iterator = list.begin(); iterator != list.end(); ++iterator) {
			if (!(*iterator) || !(*iterator)->entity_on_hatelist)
				continue;

			struct_HateList *cur = (*iterator);
			Mob *hated = cur->entity_on_hatelist;

			if (ignore_mezzed && hated->IsMezzed())
				continue;

			if (center->IsNPC() && center->CastToNPC()->IsUnderwaterOnly() && zone->HasWaterMap()) {
				if(!zone->watermap->InLiquid(glm::vec3(hated->GetPosition()))) {
					skipped_count++;
					continue;
				}
			}

			if (cur->stored_hate_amount > hate || cur->is_entity_frenzy) {
				top_hate = hated;
				hate = cur->stored_hate_amount;
			}
		}

		if (top_hate == nullptr && skipped_count > 0) {
			if (center->GetTarget() && !(ignore_mezzed && center->GetTarget()->IsMezzed()))
				return center->GetTarget();

			return nullptr;
		}

		return top_hate ? top_hate : nullptr;
	}

	return nullptr;
}

Mob *HateList::GetEntWithMostHateOnList(bool ignore_mezzed)
{
	Mob* top = nullptr;
	int64 hate = -1;

	for (auto iterator = list.begin(); iterator != list.end(); ++iterator) {
		if (!(*iterator) || !(*iterator)->entity_on_hatelist)
			continue;

		struct_HateList *cur = (*iterator);
		Mob *hated = cur->entity_on_hatelist;

		if (ignore_mezzed && hated->IsMezzed())
			continue;

		if (cur->stored_hate_amount > hate) {
			top = hated;
			hate = cur->stored_hate_amount;
		}
	}

	return top;
}


Mob *HateList::GetRandomEntOnHateList(bool ignore_mezzed)
{
	if (list.empty())
		return nullptr;

	std::vector<struct_HateList*> hate_vector;
	for (auto iterator = list.begin(); iterator != list.end(); ++iterator) {
		if ((*iterator) && (*iterator)->entity_on_hatelist && !(ignore_mezzed && (*iterator)->entity_on_hatelist->IsMezzed())) {
			hate_vector.push_back(*iterator);
		}
	}
	
	if (hate_vector.empty())
		return nullptr;
	else if (hate_vector.size() == 1)
		return hate_vector.front()->entity_on_hatelist;
	else
		return hate_vector.at(zone->random.Int(0, (hate_vector.size() - 1)))->entity_on_hatelist;
}

int32 HateList::GetEntHateAmount(Mob *in_entity, bool damage)
{
	struct_HateList *entity;

	entity = Find(in_entity);

	if (entity && damage)
		return entity->hatelist_damage;
	else if (entity)
		return entity->stored_hate_amount;
	else
		return 0;
}

bool HateList::IsAwakeEntOnHateList()
{
	for (auto iterator = list.begin(); iterator != list.end(); ++iterator) {
		if ((*iterator) && (*iterator)->entity_on_hatelist && !(*iterator)->entity_on_hatelist->IsMezzed()) {
			return true;
		}
	}

	return false;
}

bool HateList::IsHateListEmpty()
{
	return list.empty();
}

void HateList::PrintHateListToClient(Client *c)
{
	if (!c)
		return;

	for (auto iterator = list.begin(); iterator != list.end(); ++iterator) {
		struct_HateList *e = (*iterator);
		if (e) {
			Mob *hated = e->entity_on_hatelist;
			if (hated) {
				c->Message(0, "- name: %s, damage: %d, hate: %d", hated->GetName() ? hated->GetName() : "(null)", e->hatelist_damage, e->stored_hate_amount);
			}
			else {
				c->Message(0, "- <null (Mob *)entity_on_hatelist>");
			}
		}
		else {
			c->Message(0, " - <null (*)struct_HateList>");
		}
	}
}

int HateList::AreaRampage(Mob *caster, Mob *target, int count, ExtraAttackOptions *opts)
{
	if (!target || !caster)
		return 0;

	int ret = 0;
	std::list<uint32> id_list;
	auto iterator = list.begin();
	while (iterator != list.end())
	{
		struct_HateList *h = (*iterator);
		++iterator;
		if (h && h->entity_on_hatelist && h->entity_on_hatelist != caster)
		{
			if (caster->CombatRange(h->entity_on_hatelist))
			{
				id_list.push_back(h->entity_on_hatelist->GetID());
				++ret;
			}
		}
	}

	std::list<uint32>::iterator iter = id_list.begin();
	while (iter != id_list.end())
	{
		Mob *cur = entity_list.GetMobID((*iter));
		if (cur)
		{
			for (int i = 0; i < count; ++i) {
				caster->Attack(cur, MainPrimary, false, false, false, opts);
			}
		}
		iter++;
	}

	return ret;
}

void HateList::SpellCast(Mob *caster, uint32 spell_id, float range, Mob* ae_center)
{
	if (!caster)
		return;

	Mob* center = caster;

	if (ae_center)
		center = ae_center;

	//this is slower than just iterating through the list but avoids
	//crashes when people kick the bucket in the middle of this call
	//that invalidates our iterator but there's no way to know sadly
	//So keep a list of entity ids and look up after
	std::list<uint32> id_list;
	range = range * range;
	float min_range2 = spells[spell_id].min_range * spells[spell_id].min_range;
	float dist_targ = 0;
	auto iterator = list.begin();
	while (iterator != list.end())
	{
		struct_HateList *h = (*iterator);
		if (range > 0)
		{
			dist_targ = DistanceSquared(center->GetPosition(), h->entity_on_hatelist->GetPosition());
			if (dist_targ <= range && dist_targ >= min_range2)
			{
				id_list.push_back(h->entity_on_hatelist->GetID());
				h->entity_on_hatelist->CalcSpellPowerDistanceMod(spell_id, dist_targ);
			}
		}
		else
		{
			id_list.push_back(h->entity_on_hatelist->GetID());
			h->entity_on_hatelist->CalcSpellPowerDistanceMod(spell_id, 0, caster);
		}
		++iterator;
	}

	std::list<uint32>::iterator iter = id_list.begin();
	while (iter != id_list.end())
	{
		Mob *cur = entity_list.GetMobID((*iter));
		if (cur)
		{
			caster->SpellOnTarget(spell_id, cur);
		}
		iter++;
	}
}
