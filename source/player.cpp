////////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
////////////////////////////////////////////////////////////////////////
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
////////////////////////////////////////////////////////////////////////
#include "otpch.h"
#include <iostream>

#include "player.h"
#include "manager.h"

#include "iologindata.h"
#include "ioban.h"

#include "town.h"
#include "house.h"
#include "beds.h"

#include "combat.h"
#include "movement.h"
#include "weapons.h"
#include "creatureevent.h"

#include "configmanager.h"
#include "game.h"
#include "chat.h"

extern ConfigManager g_config;
extern Game g_game;
extern Chat g_chat;
extern MoveEvents* g_moveEvents;
extern Weapons* g_weapons;
extern CreatureEvents* g_creatureEvents;

AutoList<Player> Player::castAutoList; //CAST edited by Yan Liima
AutoList<Player> Player::autoList;
AutoList<ProtocolGame> Player::cSpectators;
uint32_t Player::nextSpectator = 0;
#ifdef __ENABLE_SERVER_DIAGNOSTIC__
uint32_t Player::playerCount = 0;
#endif
MuteCountMap Player::muteCountMap;

Player::Player(const std::string& _name, ProtocolGame* p):
	Creature(), transferContainer(ITEM_LOCKER), name(_name), nameDescription(_name), client(p)
{
	if(client)
		client->setPlayer(this);

	pzLocked = isConnecting = addAttackSkillPoint = requestedOutfit = false;
	saving = true;

	lastAttackBlockType = BLOCK_NONE;
	chaseMode = CHASEMODE_STANDSTILL;
	fightMode = FIGHTMODE_ATTACK;
	tradeState = TRADE_NONE;
	accountManager = MANAGER_NONE;
	guildLevel = GUILDLEVEL_NONE;

	promotionLevel = walkTaskEvent = actionTaskEvent = nextStepEvent = bloodHitCount = shieldBlockCount = 0;
	lastAttack = idleTime = marriage = blessings = balance = premiumDays = mana = manaMax = manaSpent = extraAttackSpeed = 0;
	soul = guildId = levelPercent = magLevelPercent = magLevel = experience = damageImmunities = 0;
	conditionImmunities = conditionSuppressions = groupId = vocationId = managerNumber2 = town = skullEnd = 0;
	lastLogin = lastLogout = lastIP = messageTicks = messageBuffer = nextAction = nextExAction = 0;
	editListId = maxWriteLen = windowTextId = rankId = 0;
	managerNumber3 = g_config.getNumber(ConfigManager::SPAWNTOWN_ID);

	purchaseCallback = saleCallback = -1;
	level = shootRange = 1;
	rates[SKILL__MAGLEVEL] = rates[SKILL__LEVEL] = 1.0f;
	soulMax = 100;
	capacity = 400.00;
	stamina = STAMINA_MAX;
	lastLoad = lastPing = lastPong = OTSYS_TIME();

	writeItem = NULL;
	group = NULL;
	editHouse = NULL;
	shopOwner = NULL;
	tradeItem = NULL;
	tradePartner = NULL;
	walkTask = NULL;
	weapon = NULL;

	setVocation(0);
	setParty(NULL);

	transferContainer.setParent(NULL);
	for(int32_t i = 0; i < 11; i++)
	{
		inventory[i] = NULL;
		inventoryAbilities[i] = false;
	}

	for(int32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i)
	{
		skills[i][SKILL_LEVEL] = 10;
		skills[i][SKILL_TRIES] = skills[i][SKILL_PERCENT] = 0;
		rates[i] = 1.0f;
	}

	for(int32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i)
		varSkills[i] = 0;

	for(int32_t i = STAT_FIRST; i <= STAT_LAST; ++i)
		varStats[i] = 0;

	for(int32_t i = LOSS_FIRST; i <= LOSS_LAST; ++i)
		lossPercent[i] = 100;

	for(int32_t i = 0; i <= 12; i++)
		talkState[i] = false;
#ifdef __ENABLE_SERVER_DIAGNOSTIC__

	playerCount++;
#endif
}

Player::~Player()
{
#ifdef __ENABLE_SERVER_DIAGNOSTIC__
	playerCount--;
#endif
	setWriteItem(NULL);
	for(int32_t i = 0; i < 11; i++)
	{
		if(inventory[i])
		{
			inventory[i]->setParent(NULL);
			inventory[i]->unRef();

			inventory[i] = NULL;
			inventoryAbilities[i] = false;
		}
	}

	setNextWalkActionTask(NULL);
	transferContainer.setParent(NULL);
	for(DepotMap::iterator it = depots.begin(); it != depots.end(); it++)
		it->second.first->unRef();
}

void Player::setCasting(bool c) //CAST
{
	if(cast.isCasting == c)
		return;

	if(cast.isCasting && !c) {
		castAutoList.erase(id);             
        kickCastViewers();
	}
	else {
		castAutoList[id] = this;
			
		ChatChannel* channel = g_chat.createChannel(this, CHANNEL_PRIVATE);
		if(channel && channel->addUser(this))
			sendCreatePrivateChannel(channel->getId(), channel->getName());
	}

	cast.isCasting = c;
}

void Player::setVocation(uint32_t id)
{
	vocationId = id;
	vocation = Vocations::getInstance()->getVocation(id);

	soulMax = vocation->getGain(GAIN_SOUL);
	if(Condition* condition = getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT))
	{
		condition->setParam(CONDITIONPARAM_HEALTHGAIN, vocation->getGainAmount(GAIN_HEALTH));
		condition->setParam(CONDITIONPARAM_HEALTHTICKS, (vocation->getGainTicks(GAIN_HEALTH) * 1000));
		condition->setParam(CONDITIONPARAM_MANAGAIN, vocation->getGainAmount(GAIN_MANA));
		condition->setParam(CONDITIONPARAM_MANATICKS, (vocation->getGainTicks(GAIN_MANA) * 1000));
	}
}

bool Player::isPushable() const
{
	return accountManager == MANAGER_NONE && !hasFlag(PlayerFlag_CannotBePushed) && Creature::isPushable();
}

std::string Player::getDescription(int32_t lookDistance) const
{
	std::stringstream s;
	if(lookDistance == -1)
	{
		s << "yourself.";
		if(hasFlag(PlayerFlag_ShowGroupNameInsteadOfVocation))
			s << " You are " << group->getName();
		else if(vocationId != 0)
			s << " You are " << vocation->getDescription();
		else
			s << " You have no vocation";
	}
	else
	{
		s << nameDescription;
		if(!hasCustomFlag(PlayerCustomFlag_HideLevel))
			s << " (Level " << level << ")";

		s << ". " << (sex % 2 ? "He" : "She");
		if(hasFlag(PlayerFlag_ShowGroupNameInsteadOfVocation))
			s << " is " << group->getName();
		else if(vocationId != 0)
			s << " is " << vocation->getDescription();
		else
			s << " has no vocation";

		s << getSpecialDescription();
	}

	std::string tmp;
	if(marriage && IOLoginData::getInstance()->getNameByGuid(marriage, tmp))
	{
		s << ", ";
		if(vocationId == 0)
		{
			if(lookDistance == -1)
				s << "and you are";
			else
				s << "and is";

			s << " ";
		}

		s << (sex % 2 ? "husband" : "wife") << " of " << tmp;
	}

	s << ".";
	if(guildId)
	{
		if(lookDistance == -1)
			s << " You are ";
		else
			s << " " << (sex % 2 ? "He" : "She") << " is ";

		s << (rankName.empty() ? "a member" : rankName)<< " of the " << guildName;
		if(!guildNick.empty())
			s << " (" << guildNick << ")";

		s << ".";
	}

	return s.str();
}

Item* Player::getInventoryItem(slots_t slot) const
{
	if(slot > SLOT_PRE_FIRST && slot < SLOT_LAST)
		return inventory[slot];

	if(slot == SLOT_HAND)
		return inventory[SLOT_LEFT] ? inventory[SLOT_LEFT] : inventory[SLOT_RIGHT];

	return NULL;
}

Item* Player::getEquippedItem(slots_t slot) const
{
	Item* item = getInventoryItem(slot);
	if(!item)
		return NULL;

	switch(slot)
	{
		case SLOT_LEFT:
		case SLOT_RIGHT:
			return item->getWieldPosition() == SLOT_HAND ? item : NULL;

		default:
			break;
	}

	return item->getWieldPosition() == slot ? item : NULL;
}

void Player::setConditionSuppressions(uint32_t conditions, bool remove)
{
	if(remove)
		conditionSuppressions &= ~conditions;
	else
		conditionSuppressions |= conditions;
}

Item* Player::getWeapon(bool ignoreAmmo)
{
	if(weapon)
		return weapon;

	Item* item = NULL;
	for(int32_t slot = SLOT_RIGHT; slot <= SLOT_LEFT; ++slot)
	{
		if(!(item = getEquippedItem((slots_t)slot)) || item->getWeaponType() != WEAPON_DIST)
			continue;

		if(!ignoreAmmo && item->getAmmoType() != AMMO_NONE)
		{
			Item* ammoItem = getInventoryItem(SLOT_AMMO);
			if(ammoItem && ammoItem->getAmmoType() == item->getAmmoType())
			{
				if(g_weapons->getWeapon(ammoItem))
				{
					shootRange = item->getShootRange();
					return ammoItem;
				}
			}
		}
		else if(g_weapons->getWeapon(item))
		{
			shootRange = item->getShootRange();
			return item;
		}
	}

	return NULL;
}

ItemVector Player::getWeapons() const
{
	Item* item = NULL;
	ItemVector weapons;
	for(int32_t slot = SLOT_RIGHT; slot <= SLOT_LEFT; ++slot)
	{
		if(!(item = getEquippedItem((slots_t)slot)))
			continue;

		switch(item->getWeaponType())
		{
			case WEAPON_DIST:
				if(item->getAmmoType() != AMMO_NONE)
					break;

			case WEAPON_SWORD:
			case WEAPON_CLUB:
			case WEAPON_AXE:
			case WEAPON_FIST:
			case WEAPON_WAND:
			{
				if(g_weapons->getWeapon(item))
					weapons.push_back(item);

				break;
			}

			default:
				break;
		}
	}

	return weapons;
}

void Player::updateWeapon()
{
	ItemVector weapons = getWeapons();
	if(weapons.empty())
		weapon = NULL;
	else if(!weapon || weapons.size() == 1 || weapons[1] == weapon)
		weapon = weapons[0];
	else if(weapons[0] == weapon)
		weapon = weapons[1];
	else
		weapon = NULL;

	if(weapon)
		shootRange = weapon->getShootRange();
}

WeaponType_t Player::getWeaponType()
{
	if(Item* item = getWeapon(false))
		return item->getWeaponType();

	return WEAPON_NONE;
}

int32_t Player::getWeaponSkill(const Item* item) const
{
	if(!item)
		return getSkill(SKILL_FIST, SKILL_LEVEL);

	switch(item->getWeaponType())
	{
		case WEAPON_SWORD:
			return getSkill(SKILL_SWORD, SKILL_LEVEL);

		case WEAPON_CLUB:
			return getSkill(SKILL_CLUB, SKILL_LEVEL);

		case WEAPON_AXE:
			return getSkill(SKILL_AXE, SKILL_LEVEL);

		case WEAPON_FIST:
			return getSkill(SKILL_FIST, SKILL_LEVEL);

		case WEAPON_DIST:
			return getSkill(SKILL_DIST, SKILL_LEVEL);

		default:
			break;
	}

	return 0;
}

int32_t Player::getArmor() const
{
	int32_t i = SLOT_FIRST, armor = 0;
	for(; i < SLOT_LAST; ++i)
	{
		if(Item* item = getInventoryItem((slots_t)i))
			armor += item->getArmor();
	}

	if(vocation->getMultiplier(MULTIPLIER_ARMOR) != 1.0)
		return int32_t(armor * vocation->getMultiplier(MULTIPLIER_ARMOR));

	return armor;
}

void Player::getShieldAndWeapon(const Item* &_shield, const Item* &_weapon) const
{
	_shield = NULL;
	Item* item = NULL;
	for(uint32_t slot = SLOT_RIGHT; slot <= SLOT_LEFT; ++slot)
	{
		if(!(item = getInventoryItem((slots_t)slot)) || item->getWeaponType() != WEAPON_SHIELD)
			continue;

		if(!_shield || (_shield && item->getDefense() > _shield->getDefense()))
			_shield = item;
	}

	_weapon = weapon;
}

int32_t Player::getDefense() const
{
	int32_t baseDefense = 5, defenseValue = 0, defenseSkill = 0, extraDefense = 0;
	float defenseFactor = getDefenseFactor();

	const Item *_weapon = NULL, *_shield = NULL;
	getShieldAndWeapon(_shield, _weapon);
	if(_weapon)
	{
		extraDefense = _weapon->getExtraDefense();
		defenseValue = baseDefense + _weapon->getDefense();
		defenseSkill = getWeaponSkill(_weapon);
	}

	if(_shield && _shield->getDefense() > defenseValue)
	{
		if(_shield->getExtraDefense() > extraDefense)
			extraDefense = _shield->getExtraDefense();

		defenseValue = baseDefense + _shield->getDefense();
		defenseSkill = getSkill(SKILL_SHIELD, SKILL_LEVEL);
	}

	if(!defenseSkill)
		return 0;

	defenseValue += extraDefense;
	if(vocation->getMultiplier(MULTIPLIER_DEFENSE) != 1.0)
		defenseValue = int32_t(defenseValue * vocation->getMultiplier(MULTIPLIER_DEFENSE));

	return ((int32_t)std::ceil(((float)(defenseSkill * (defenseValue * 0.015)) + (defenseValue * 0.1)) * defenseFactor));
}

float Player::getAttackFactor() const
{
	switch(fightMode)
	{
		case FIGHTMODE_BALANCED:
			return 1.2f;

		case FIGHTMODE_DEFENSE:
			return 2.0f;

		case FIGHTMODE_ATTACK:
		default:
			break;
	}

	return 1.0f;
}

float Player::getDefenseFactor() const
{
	switch(fightMode)
	{
		case FIGHTMODE_BALANCED:
			return 1.2f;

		case FIGHTMODE_DEFENSE:
		{
			if((OTSYS_TIME() - lastAttack) < getAttackSpeed()) //attacking will cause us to get into normal defense
				return 1.0f;

			return 2.0f;
		}

		case FIGHTMODE_ATTACK:
		default:
			break;
	}

	return 1.0f;
}

void Player::sendIcons() const
{
	if(!client)
		return;

	uint32_t icons = 0;
	for(ConditionList::const_iterator it = conditions.begin(); it != conditions.end(); ++it)
	{
		if(!isSuppress((*it)->getType()))
			icons |= (*it)->getIcons();
	}

	if(getZone() == ZONE_PROTECTION) //Se preciso deixa s� esse
	{
		icons |= ICON_PROTECTIONZONE; //E esse, by Yan Liima.
		if(hasBitSet(ICON_SWORDS, icons))
			icons &= ~ICON_SWORDS;
	}

	if(pzLocked)
		icons |= ICON_PZ;

	client->sendIcons(icons);
	
	for(AutoList<ProtocolGame>::const_iterator it = Player::cSpectators.begin(); it != Player::cSpectators.end(); ++it) //CAST
		if(it->second->getPlayer() == this)
			it->second->sendIcons(icons);
}

/*void Player::updateInventoryWeight()
{
	inventoryWeight = 0.00;
	if(hasFlag(PlayerFlag_HasInfiniteCapacity))
		return;

	for(int32_t i = SLOT_FIRST; i < SLOT_LAST; ++i)
	{
		if(Item* item = getInventoryItem((slots_t)i))
			inventoryWeight += item->getWeight();
	}
}*/

void Player::updateInventoryWeight()
{
    inventoryWeight = 0.00;
 
    if(!hasFlag(PlayerFlag_HasInfiniteCapacity)){
        for(int i = SLOT_FIRST; i < SLOT_LAST; ++i){
            Item* item = getInventoryItem((slots_t)i);
            if(item){
                inventoryWeight += item->getWeight();
            }
        }
    }
}

void Player::updateInventoryGoods(uint32_t itemId)
{
	if(Item::items[itemId].worth)
	{
		sendGoods();
		return;
	}

	for(ShopInfoList::iterator it = shopOffer.begin(); it != shopOffer.end(); ++it)
	{
		if(it->itemId != itemId)
			continue;

		sendGoods();
		break;
	}
}

int32_t Player::getPlayerInfo(playerinfo_t playerinfo) const
{
	switch(playerinfo)
	{
		case PLAYERINFO_LEVEL:
			return level;
		case PLAYERINFO_LEVELPERCENT:
			return levelPercent;
		case PLAYERINFO_MAGICLEVEL:
			return std::max((int32_t)0, ((int32_t)magLevel + varStats[STAT_MAGICLEVEL]));
		case PLAYERINFO_MAGICLEVELPERCENT:
			return magLevelPercent;
		case PLAYERINFO_HEALTH:
			return health;
		case PLAYERINFO_MAXHEALTH:
			return std::max((int32_t)1, ((int32_t)healthMax + varStats[STAT_MAXHEALTH]));
		case PLAYERINFO_MANA:
			return mana;
		case PLAYERINFO_MAXMANA:
			return std::max((int32_t)0, ((int32_t)manaMax + varStats[STAT_MAXMANA]));
		case PLAYERINFO_SOUL:
			return std::max((int32_t)0, ((int32_t)soul + varStats[STAT_SOUL]));
		default:
			break;
	}

	return 0;
}

int32_t Player::getSkill(skills_t skilltype, skillsid_t skillinfo) const
{
	int32_t ret = skills[skilltype][skillinfo];
	if(skillinfo == SKILL_LEVEL)
		ret += varSkills[skilltype];

	return std::max((int32_t)0, ret);
}

void Player::addSkillAdvance(skills_t skill, uint64_t count, bool useMultiplier/* = true*/)
{
	if(!count)
		return;

	//player has reached max skill
	uint64_t currReqTries = vocation->getReqSkillTries(skill, skills[skill][SKILL_LEVEL]),
		nextReqTries = vocation->getReqSkillTries(skill, skills[skill][SKILL_LEVEL] + 1);
	if(currReqTries > nextReqTries)
		return;

	if(useMultiplier)
		count = uint64_t((double)count * rates[skill] * g_config.getDouble(ConfigManager::RATE_SKILL));

	std::stringstream s;
	while(skills[skill][SKILL_TRIES] + count >= nextReqTries)
	{
		count -= nextReqTries - skills[skill][SKILL_TRIES];
	 	skills[skill][SKILL_TRIES] = skills[skill][SKILL_PERCENT] = 0;
		skills[skill][SKILL_LEVEL]++;

		s.str("");
		s << "You advanced in " << getSkillName(skill);
		if(g_config.getBool(ConfigManager::ADVANCING_SKILL_LEVEL))
			s << " [" << skills[skill][SKILL_LEVEL] << "]";

		s << ".";
		sendTextMessage(MSG_EVENT_ADVANCE, s.str().c_str());

		CreatureEventList advanceEvents = getCreatureEvents(CREATURE_EVENT_ADVANCE);
		for(CreatureEventList::iterator it = advanceEvents.begin(); it != advanceEvents.end(); ++it)
			(*it)->executeAdvance(this, skill, (skills[skill][SKILL_LEVEL] - 1), skills[skill][SKILL_LEVEL]);

		currReqTries = nextReqTries;
		nextReqTries = vocation->getReqSkillTries(skill, skills[skill][SKILL_LEVEL] + 1);
		if(currReqTries > nextReqTries)
		{
			count = 0;
			break;
		}
	}

	if(count)
		skills[skill][SKILL_TRIES] += count;

	//update percent
	uint16_t newPercent = Player::getPercentLevel(skills[skill][SKILL_TRIES], nextReqTries);
 	if(skills[skill][SKILL_PERCENT] != newPercent)
	{
		skills[skill][SKILL_PERCENT] = newPercent;
		sendSkills();
 	}
	else if(!s.str().empty())
		sendSkills();
}

void Player::setVarStats(stats_t stat, int32_t modifier)
{
	varStats[stat] += modifier;
	switch(stat)
	{
		case STAT_MAXHEALTH:
		{
			if(getHealth() > getMaxHealth())
				Creature::changeHealth(getMaxHealth() - getHealth());
			else
				g_game.addCreatureHealth(this);

			break;
		}

		case STAT_MAXMANA:
		{
			if(getMana() > getMaxMana())
				Creature::changeMana(getMaxMana() - getMana());

			break;
		}

		default:
			break;
	}
}

int32_t Player::getDefaultStats(stats_t stat)
{
	switch(stat)
	{
		case STAT_MAGICLEVEL:
			return getMagicLevel() - getVarStats(STAT_MAGICLEVEL);
		case STAT_MAXHEALTH:
			return getMaxHealth() - getVarStats(STAT_MAXHEALTH);
		case STAT_MAXMANA:
			return getMaxMana() - getVarStats(STAT_MAXMANA);
		case STAT_SOUL:
			return getSoul() - getVarStats(STAT_SOUL);
		default:
			break;
	}

	return 0;
}

Container* Player::getContainer(uint32_t cid)
{
	for(ContainerVector::iterator it = containerVec.begin(); it != containerVec.end(); ++it)
	{
		if(it->first == cid)
			return it->second;
	}

	return NULL;
}

int32_t Player::getContainerID(const Container* container) const
{
	for(ContainerVector::const_iterator cl = containerVec.begin(); cl != containerVec.end(); ++cl)
	{
		if(cl->second == container)
			return cl->first;
	}

	return -1;
}

void Player::addContainer(uint32_t cid, Container* container)
{
#ifdef __DEBUG__
	std::clog << getName() << ", addContainer: " << (int32_t)cid << std::endl;
#endif
	if(cid > 0xF)
		return;

	for(ContainerVector::iterator cl = containerVec.begin(); cl != containerVec.end(); ++cl)
	{
		if(cl->first == cid)
		{
			cl->second = container;
			return;
		}
	}

	containerVec.push_back(std::make_pair(cid, container));
}

void Player::closeContainer(uint32_t cid)
{
	for(ContainerVector::iterator cl = containerVec.begin(); cl != containerVec.end(); ++cl)
	{
		if(cl->first == cid)
		{
			containerVec.erase(cl);
			break;
		}
	}
#ifdef __DEBUG__

	std::clog << getName() << ", closeContainer: " << (int32_t)cid << std::endl;
#endif
}

bool Player::canOpenCorpse(uint32_t ownerId)
{
	return guid == ownerId || (party && party->canOpenCorpse(ownerId)) || hasCustomFlag(PlayerCustomFlag_GamemasterPrivileges);
}

uint16_t Player::getLookCorpse() const
{
	if(sex % 2)
		return ITEM_MALE_CORPSE;

	return ITEM_FEMALE_CORPSE;
}

void Player::dropLoot(Container* corpse)
{
	if(!corpse || lootDrop != LOOT_DROP_FULL)
		return;

	uint32_t loss = lossPercent[LOSS_CONTAINERS];
	if(g_config.getBool(ConfigManager::BLESSINGS))
	{
		uint32_t start = g_config.getNumber(ConfigManager::BLESS_REDUCTION_BASE), bless = getBlessings();
		while(bless > 0 && loss > 0)
		{
			loss -= start;
			start -= g_config.getNumber(ConfigManager::BLESS_REDUCTION_DECREMENT);
			bless--;
		}
	}

	uint32_t itemLoss = (uint32_t)std::floor((5. + loss) * lossPercent[LOSS_ITEMS] / 1000.);
	for(int32_t i = SLOT_FIRST; i < SLOT_LAST; ++i)
	{
		Item* item = inventory[i];
		if(!item)
			continue;

		uint32_t tmp = random_range(1, 100);
		if(skull > SKULL_WHITE || (item->getContainer() && tmp < loss) || (!item->getContainer() && tmp < itemLoss))
		{
			g_game.internalMoveItem(NULL, this, corpse, INDEX_WHEREEVER, item, item->getItemCount(), 0);
			sendRemoveInventoryItem((slots_t)i, inventory[(slots_t)i]);
		}
	}
}

bool Player::setStorage(const std::string& key, const std::string& value)
{
	uint32_t numericKey = atol(key.c_str());
	if(!IS_IN_KEYRANGE(numericKey, RESERVED_RANGE))
		return Creature::setStorage(key, value);

	if(IS_IN_KEYRANGE(numericKey, OUTFITS_RANGE))
	{
		uint32_t lookType = atoi(value.c_str()) >> 16, addons = atoi(value.c_str()) & 0xFF;
		if(addons < 4)
		{
			Outfit outfit;
			if(Outfits::getInstance()->getOutfit(lookType, outfit))
				return addOutfit(outfit.outfitId, addons);
		}
		else
			std::clog << "[Warning - Player::setStorage] Invalid addons value key: " << key
				<< ", value: " << value << " for player: " << getName() << std::endl;
	}
	else if(IS_IN_KEYRANGE(numericKey, OUTFITSID_RANGE))
	{
		uint32_t outfitId = atoi(value.c_str()) >> 16, addons = atoi(value.c_str()) & 0xFF;
		if(addons < 4)
			return addOutfit(outfitId, addons);
		else
			std::clog << "[Warning - Player::setStorage] Invalid addons value key: " << key
				<< ", value: " << value << " for player: " << getName() << std::endl;
	}
	else
		std::clog << "[Warning - Player::setStorage] Unknown reserved key: " << key << " for player: " << getName() << std::endl;

	return false;
}

void Player::eraseStorage(const std::string& key)
{
	Creature::eraseStorage(key);
	if(IS_IN_KEYRANGE(atol(key.c_str()), RESERVED_RANGE))
		std::clog << "[Warning - Player::eraseStorage] Unknown reserved key: " << key << " for player: " << name << std::endl;
}

bool Player::canSee(const Position& pos) const
{
	if(client)
		return client->canSee(pos);

	return false;
}

bool Player::canSeeCreature(const Creature* creature) const
{
	if(creature == this)
		return true;

	if(const Player* player = creature->getPlayer())
		return !player->isGhost() || getGhostAccess() >= player->getGhostAccess();

	return !creature->isInvisible() || canSeeInvisibility();
}

bool Player::canWalkthrough(const Creature* creature) const
{
	if(creature == this || hasCustomFlag(PlayerCustomFlag_CanWalkthrough) || creature->isWalkable() ||
		(creature->getMaster() && creature->getMaster() != this && canWalkthrough(creature->getMaster())))
		return true;

	const Player* player = creature->getPlayer();
	if(!player)
		return false;

	if((((g_game.getWorldType() == WORLDTYPE_OPTIONAL &&
#ifdef __WAR_SYSTEM__
		!player->isEnemy(this, true) &&
#endif
		player->getVocation()->isAttackable()) || player->getTile()->hasFlag(TILESTATE_PROTECTIONZONE) || (player->getVocation()->isAttackable() &&
        player->getLevel() < (uint32_t)g_config.getNumber(ConfigManager::PROTECTION_LEVEL))) && player->getTile()->ground) &&
        Item::items[player->getTile()->ground->getID()].walkStack && (!player->hasCustomFlag(PlayerCustomFlag_GamemasterPrivileges)
        || player->getAccess() <= getAccess()))
        return true;

    return (player->isGhost() && getGhostAccess() < player->getGhostAccess())
        || (isGhost() && getGhostAccess() > player->getGhostAccess());
}

Depot* Player::getDepot(uint32_t depotId, bool autoCreateDepot)
{
	DepotMap::iterator it = depots.find(depotId);
	if(it != depots.end())
		return it->second.first;

	//create a new depot?
	if(autoCreateDepot)
	{
		Item* locker = Item::CreateItem(ITEM_LOCKER);
		if(Container* container = locker->getContainer())
		{
			if(Depot* depot = container->getDepot())
			{
				container->__internalAddThing(Item::CreateItem(ITEM_DEPOT));
				addDepot(depot, depotId);
				return depot;
			}
		}

		g_game.freeThing(locker);
		std::clog << "Failure: Creating a new depot with id: " << depotId <<
			", for player: " << getName() << std::endl;
	}

	return NULL;
}

bool Player::addDepot(Depot* depot, uint32_t depotId)
{
	if(getDepot(depotId, false))
		return false;

	depots[depotId] = std::make_pair(depot, false);
	depot->setMaxDepotLimit((group != NULL ? group->getDepotLimit(isPremium()) : 1000));
	return true;
}

void Player::useDepot(uint32_t depotId, bool value)
{
	DepotMap::iterator it = depots.find(depotId);
	if(it != depots.end())
		depots[depotId] = std::make_pair(it->second.first, value);
}

void Player::sendCancelMessage(ReturnValue message) const
{
	switch(message)
	{
		case RET_DESTINATIONOUTOFREACH:
			sendCancel("Destination is out of reach.");
			break;

		case RET_NOTMOVABLE:
			sendCancel("You cannot move this object.");
			break;

		case RET_DROPTWOHANDEDITEM:
			sendCancel("Drop the double-handed object first.");
			break;

		case RET_BOTHHANDSNEEDTOBEFREE:
			sendCancel("Both hands need to be free.");
			break;

		case RET_CANNOTBEDRESSED:
			sendCancel("You cannot dress this object there.");
			break;

		case RET_PUTTHISOBJECTINYOURHAND:
			sendCancel("Put this object in your hand.");
			break;

		case RET_PUTTHISOBJECTINBOTHHANDS:
			sendCancel("Put this object in both hands.");
			break;

		case RET_CANONLYUSEONEWEAPON:
			sendCancel("You may use only one weapon.");
			break;

		case RET_TOOFARAWAY:
			sendCancel("Too far away.");
			break;

		case RET_FIRSTGODOWNSTAIRS:
			sendCancel("First go downstairs.");
			break;

		case RET_FIRSTGOUPSTAIRS:
			sendCancel("First go upstairs.");
			break;

		case RET_NOTENOUGHCAPACITY:
			sendCancel("This object is too heavy for you to carry.");
			break;

		case RET_CONTAINERNOTENOUGHROOM:
			sendCancel("You cannot put more objects in this container.");
			break;

		case RET_NEEDEXCHANGE:
		case RET_NOTENOUGHROOM:
			sendCancel("There is not enough room.");
			break;

		case RET_CANNOTPICKUP:
			sendCancel("You cannot take this object.");
			break;

		case RET_CANNOTTHROW:
			sendCancel("You cannot throw there.");
			break;

		case RET_THEREISNOWAY:
			sendCancel("There is no way.");
			break;

		case RET_THISISIMPOSSIBLE:
			sendCancel("This is impossible.");
			break;

		case RET_PLAYERISPZLOCKED:
			sendCancel("You cannot enter a protection zone after attacking another player.");
			break;

		case RET_PLAYERISNOTINVITED:
			sendCancel("You are not invited.");
			break;

		case RET_CREATUREDOESNOTEXIST:
			sendCancel("Creature does not exist.");
			break;

		case RET_DEPOTISFULL:
			sendCancel("You cannot put more items in this depot.");
			break;

		case RET_CANNOTUSETHISOBJECT:
			sendCancel("You cannot use this object.");
			break;

		case RET_PLAYERWITHTHISNAMEISNOTONLINE:
			sendCancel("A player with this name is not online.");
			break;

		case RET_NOTREQUIREDLEVELTOUSERUNE:
			sendCancel("You do not have the required magic level to use this rune.");
			break;

		case RET_YOUAREALREADYTRADING:
			sendCancel("You are already trading.");
			break;

		case RET_THISPLAYERISALREADYTRADING:
			sendCancel("This player is already trading.");
			break;

		case RET_YOUMAYNOTLOGOUTDURINGAFIGHT:
			sendCancel("You may not logout during or immediately after a fight!");
			break;

		case RET_DIRECTPLAYERSHOOT:
			sendCancel("You are not allowed to shoot directly on players.");
			break;

		case RET_NOTENOUGHLEVEL:
			sendCancel("You do not have enough level.");
			break;

		case RET_NOTENOUGHMAGICLEVEL:
			sendCancel("You do not have enough magic level.");
			break;

		case RET_NOTENOUGHMANA:
			sendCancel("You do not have enough mana.");
			break;

		case RET_NOTENOUGHSOUL:
			sendCancel("You do not have enough soul.");
			break;

		case RET_YOUAREEXHAUSTED:
			sendCancel("You are exhausted.");
			break;

		case RET_CANONLYUSETHISRUNEONCREATURES:
			sendCancel("You can only use this rune on creatures.");
			break;

		case RET_PLAYERISNOTREACHABLE:
			sendCancel("Player is not reachable.");
			break;

		case RET_CREATUREISNOTREACHABLE:
			sendCancel("Creature is not reachable.");
			break;

		case RET_ACTIONNOTPERMITTEDINPROTECTIONZONE:
			sendCancel("This action is not permitted in a protection zone.");
			break;

		case RET_YOUMAYNOTATTACKTHISPLAYER:
			sendCancel("You may not attack this player.");
			break;

		case RET_YOUMAYNOTATTACKTHISCREATURE:
			sendCancel("You may not attack this creature.");
			break;

		case RET_YOUMAYNOTATTACKAPERSONINPROTECTIONZONE:
			sendCancel("You may not attack a person in a protection zone.");
			break;

		case RET_YOUMAYNOTATTACKAPERSONWHILEINPROTECTIONZONE:
			sendCancel("You may not attack a person while you are in a protection zone.");
			break;

		case RET_YOUCANONLYUSEITONCREATURES:
			sendCancel("You can only use it on creatures.");
			break;

		case RET_TURNSECUREMODETOATTACKUNMARKEDPLAYERS:
			sendCancel("Turn secure mode off if you really want to attack unmarked players.");
			break;

		case RET_YOUNEEDPREMIUMACCOUNT:
			sendCancel("You need a premium account.");
			break;

		case RET_YOUNEEDTOLEARNTHISSPELL:
			sendCancel("You need to learn this spell first.");
			break;

		case RET_YOURVOCATIONCANNOTUSETHISSPELL:
			sendCancel("Your vocation cannot use this spell.");
			break;

		case RET_YOUNEEDAWEAPONTOUSETHISSPELL:
			sendCancel("You need to equip a weapon to use this spell.");
			break;

		case RET_PLAYERISPZLOCKEDLEAVEPVPZONE:
			sendCancel("You cannot leave a pvp zone after attacking another player.");
			break;

		case RET_PLAYERISPZLOCKEDENTERPVPZONE:
			sendCancel("You cannot enter a pvp zone after attacking another player.");
			break;

		case RET_ACTIONNOTPERMITTEDINANOPVPZONE:
			sendCancel("This action is not permitted in a safe zone.");
			break;

		case RET_YOUCANNOTLOGOUTHERE:
			sendCancel("You cannot logout here.");
			break;

		case RET_YOUNEEDAMAGICITEMTOCASTSPELL:
			sendCancel("You need a magic item to cast this spell.");
			break;

		case RET_CANNOTCONJUREITEMHERE:
			sendCancel("You cannot conjure items here.");
			break;

		case RET_NAMEISTOOAMBIGUOUS:
			sendCancel("Name is too ambiguous.");
			break;

		case RET_CANONLYUSEONESHIELD:
			sendCancel("You may use only one shield.");
			break;

		case RET_YOUARENOTTHEOWNER:
			sendCancel("You are not the owner.");
			break;

		case RET_YOUMAYNOTCASTAREAONBLACKSKULL:
			sendCancel("You may not cast area spells while you have a black skull.");
			break;

		case RET_TILEISFULL:
			sendCancel("You cannot add more items on this tile.");
			break;

		case RET_YOUHAVETOWAIT:
			sendCancel("Sorry, you have to wait.");
			break;
			
		case RET_DONTSHOWMESSAGE:
			break;

		case RET_NOTPOSSIBLE:
		default:
			sendCancel("Sorry, not possible.");
			break;
	}
}

Item* Player::getWriteItem(uint32_t& _windowTextId, uint16_t& _maxWriteLen)
{
	_windowTextId = windowTextId;
	_maxWriteLen = maxWriteLen;
	return writeItem;
}

void Player::setWriteItem(Item* item, uint16_t _maxWriteLen/* = 0*/)
{
	windowTextId++;
	if(writeItem)
		writeItem->unRef();

	if(item)
	{
		writeItem = item;
		maxWriteLen = _maxWriteLen;
		writeItem->addRef();
	}
	else
	{
		writeItem = NULL;
		maxWriteLen = 0;
	}
}

House* Player::getEditHouse(uint32_t& _windowTextId, uint32_t& _listId)
{
	_windowTextId = windowTextId;
	_listId = editListId;
	return editHouse;
}

void Player::setEditHouse(House* house, uint32_t listId/* = 0*/)
{
	windowTextId++;
	editHouse = house;
	editListId = listId;
}

void Player::sendHouseWindow(House* house, uint32_t listId) const
{
	if(!client)
		return;

	std::string text;
	if(house->getAccessList(listId, text))
		client->sendHouseWindow(windowTextId, house, listId, text);
}

void Player::sendCreatureChangeVisible(const Creature* creature, Visible_t visible)
{
	if(!client)
		return;

	const Player* player = creature->getPlayer();
	if(player == this || (player && (visible < VISIBLE_GHOST_APPEAR || getGhostAccess() >= player->getGhostAccess()))
		|| (!player && canSeeInvisibility()))
		sendCreatureChangeOutfit(creature, creature->getCurrentOutfit());
	else if(visible == VISIBLE_DISAPPEAR || visible == VISIBLE_GHOST_DISAPPEAR)
		sendCreatureDisappear(creature, creature->getTile()->getClientIndexOfThing(this, creature));
	else
		sendCreatureAppear(creature);
}

void Player::sendAddContainerItem(const Container* container, const Item* item)
{
	if(!client)
		return;

	for(ContainerVector::const_iterator cl = containerVec.begin(); cl != containerVec.end(); ++cl)
	{
		if(cl->second == container) {
			client->sendAddContainerItem(cl->first, item);

			//CAST
			for(AutoList<ProtocolGame>::iterator it = Player::cSpectators.begin(); it != Player::cSpectators.end(); ++it)
				if(it->second->getPlayer() == this)
					it->second->sendAddContainerItem(cl->first, item);
		}
	}
}

void Player::sendUpdateContainerItem(const Container* container, uint8_t slot, const Item*, const Item* newItem)
{
	if(!client)
		return;

	for(ContainerVector::const_iterator cl = containerVec.begin(); cl != containerVec.end(); ++cl)
	{
		if(cl->second == container) {
			client->sendUpdateContainerItem(cl->first, slot, newItem);
			for(AutoList<ProtocolGame>::iterator it = Player::cSpectators.begin(); it != Player::cSpectators.end(); ++it)
				if(it->second->getPlayer() == this)
					it->second->sendUpdateContainerItem(cl->first, slot, newItem); //CAST
         }
	}
}

void Player::sendRemoveContainerItem(const Container* container, uint8_t slot, const Item*)
{
	if(!client)
		return;

	for(ContainerVector::const_iterator cl = containerVec.begin(); cl != containerVec.end(); ++cl)
	{
		if(cl->second == container) {
			client->sendRemoveContainerItem(cl->first, slot);
			for(AutoList<ProtocolGame>::iterator it = Player::cSpectators.begin(); it != Player::cSpectators.end(); ++it) //CAST
				if(it->second->getPlayer() == this)
					it->second->sendRemoveContainerItem(cl->first, slot);
		}
	}
}

void Player::onUpdateTileItem(const Tile* tile, const Position& pos, const Item* oldItem,
	const ItemType& oldType, const Item* newItem, const ItemType& newType)
{
	Creature::onUpdateTileItem(tile, pos, oldItem, oldType, newItem, newType);
	if(oldItem != newItem)
		onRemoveTileItem(tile, pos, oldType, oldItem);

	if(tradeState != TRADE_TRANSFER && tradeItem && oldItem == tradeItem)
		g_game.internalCloseTrade(this);
}

void Player::onRemoveTileItem(const Tile* tile, const Position& pos, const ItemType& iType, const Item* item)
{
	Creature::onRemoveTileItem(tile, pos, iType, item);
	if(tradeState == TRADE_TRANSFER)
		return;

	checkTradeState(item);
	if(tradeItem)
	{
		const Container* container = item->getContainer();
		if(container && container->isHoldingItem(tradeItem))
			g_game.internalCloseTrade(this);
	}
}

void Player::onCreatureAppear(const Creature* creature)
{
	Creature::onCreatureAppear(creature);
	if(creature != this)
		return;

	Item* item = NULL;
	for(int32_t slot = SLOT_FIRST; slot < SLOT_LAST; ++slot)
	{
		if(!(item = getInventoryItem((slots_t)slot)))
			continue;

		item->__startDecaying();
		g_moveEvents->onPlayerEquip(this, item, (slots_t)slot, false);
	}

	updateWeapon();
	if(BedItem* bed = Beds::getInstance()->getBedBySleeper(guid))
		bed->wakeUp();

	Outfit outfit;
	if(Outfits::getInstance()->getOutfit(defaultOutfit.lookType, outfit))
		outfitAttributes = Outfits::getInstance()->addAttributes(getID(), outfit.outfitId, sex, defaultOutfit.lookAddons);

	if(lastLogout && stamina < STAMINA_MAX)
	{
		int64_t ticks = (int64_t)time(NULL) - lastLogout - 600;
		if(ticks > 0)
		{
			ticks = (int64_t)((double)(ticks * 1000) / g_config.getDouble(ConfigManager::RATE_STAMINA_GAIN));
			int64_t premium = g_config.getNumber(ConfigManager::STAMINA_LIMIT_TOP) * STAMINA_MULTIPLIER, period = ticks;
			if((int64_t)stamina <= premium)
			{
				period += stamina;
				if(period > premium)
					period -= premium;
				else
					period = 0;

				useStamina(ticks - period);
			}

			if(period > 0)
			{
				ticks = (int64_t)((g_config.getDouble(ConfigManager::RATE_STAMINA_GAIN) * period)
					/ g_config.getDouble(ConfigManager::RATE_STAMINA_THRESHOLD));
				if(stamina + ticks > STAMINA_MAX)
					ticks = STAMINA_MAX - stamina;

				useStamina(ticks);
			}

			sendStats();
		}
	}

	g_game.checkPlayersRecord(this);
	if(!isGhost())
		IOLoginData::getInstance()->updateOnlineStatus(guid, true);

	if(g_config.getBool(ConfigManager::DISPLAY_LOGGING))
		std::clog << name << " has logged in." << std::endl;
}

void Player::onAttackedCreatureDisappear(bool isLogout)
{
	sendCancelTarget();
	if(!isLogout)
		sendTextMessage(MSG_STATUS_SMALL, "Target lost.");
}

void Player::onFollowCreatureDisappear(bool isLogout)
{
	sendCancelTarget();
	if(!isLogout)
		sendTextMessage(MSG_STATUS_SMALL, "Target lost.");
}

void Player::onChangeZone(ZoneType_t zone)
{
	if(attackedCreature && zone == ZONE_PROTECTION && !hasFlag(PlayerFlag_IgnoreProtectionZone))
	{
		setAttackedCreature(NULL);
		onAttackedCreatureDisappear(false);
	}
	sendIcons();
}

void Player::onAttackedCreatureChangeZone(ZoneType_t zone)
{
	if(zone == ZONE_PROTECTION && !hasFlag(PlayerFlag_IgnoreProtectionZone))
	{
		setAttackedCreature(NULL);
		onAttackedCreatureDisappear(false);
	}
	else if(zone == ZONE_OPTIONAL && attackedCreature->getPlayer() && !hasFlag(PlayerFlag_IgnoreProtectionZone))
	{
		setAttackedCreature(NULL);
		onAttackedCreatureDisappear(false);
	}
	else if(zone == ZONE_OPEN && g_game.getWorldType() == WORLDTYPE_OPTIONAL && attackedCreature->getPlayer()
#ifdef __WAR_SYSTEM__
		&& !attackedCreature->getPlayer()->isEnemy(this, true)
#endif
		)
	{
		//attackedCreature can leave a pvp zone if not pzlocked
		setAttackedCreature(NULL);
		onAttackedCreatureDisappear(false);
	}
}

void Player::onCreatureDisappear(const Creature* creature, bool isLogout)
{
	Creature::onCreatureDisappear(creature, isLogout);
	if(creature != this)
		return;

	if(isLogout)
	{
		loginPosition = getPosition();
		lastLogout = time(NULL);
	}

	if(eventWalk)
		setFollowCreature(NULL);

	closeShopWindow();
	if(tradePartner)
		g_game.internalCloseTrade(this);

	clearPartyInvitations();
	if(party)
		party->leave(this);

	g_game.cancelRuleViolation(this);
	if(hasFlag(PlayerFlag_CanAnswerRuleViolations))
	{
		PlayerVector closeReportList;
		for(RuleViolationsMap::const_iterator it = g_game.getRuleViolations().begin(); it != g_game.getRuleViolations().end(); ++it)
		{
			if(it->second->gamemaster == this)
				closeReportList.push_back(it->second->reporter);
		}

		for(PlayerVector::iterator it = closeReportList.begin(); it != closeReportList.end(); ++it)
			g_game.closeRuleViolation(*it);
	}

	g_chat.removeUserFromAllChannels(this);
	if(!isGhost())
		IOLoginData::getInstance()->updateOnlineStatus(guid, false);

	if(g_config.getBool(ConfigManager::DISPLAY_LOGGING))
		std::clog << getName() << " has logged out." << std::endl;

	bool saved = false;
	for(uint32_t tries = 0; !saved && tries < 3; ++tries)
	{
		if(IOLoginData::getInstance()->savePlayer(this))
			saved = true;
#ifdef __DEBUG__
		else
			std::clog << "Error while saving player: " << getName() << ", strike " << tries << "." << std::endl;
#endif
	}

	if(!saved)
#ifndef __DEBUG__
		std::clog << "Error while saving player: " << getName() << "." << std::endl;
#else
		std::clog << "Player " << getName() << " couldn't be saved." << std::endl;
#endif
}

void Player::openShopWindow()
{
	sendShop();
	sendGoods();
}

void Player::closeShopWindow(bool send/* = true*/, Npc* npc/* = NULL*/, int32_t onBuy/* = -1*/, int32_t onSell/* = -1*/)
{
	if(npc || (npc = getShopOwner(onBuy, onSell)))
		npc->onPlayerEndTrade(this, onBuy, onSell);

	if(shopOwner)
	{
		shopOwner = NULL;
		if(send)
			sendCloseShop();
	}

	purchaseCallback = saleCallback = -1;
	shopOffer.clear();
}

bool Player::canShopItem(uint16_t itemId, uint8_t subType, ShopEvent_t event)
{
	for(ShopInfoList::iterator sit = shopOffer.begin(); sit != shopOffer.end(); ++sit)
	{
		if(sit->itemId != itemId || ((event != SHOPEVENT_BUY || sit->buyPrice < 0)
			&& (event != SHOPEVENT_SELL || sit->sellPrice < 0)))
			continue;

		if(event == SHOPEVENT_SELL)
			return true;

		const ItemType& it = Item::items[id];
		if(it.isFluidContainer() || it.isSplash())
			return sit->subType == subType;

		return true;
	}

	return false;
}

void Player::onWalk(Direction& dir)
{
	Creature::onWalk(dir);
	setNextActionTask(NULL);
	//setNextAction(OTSYS_TIME() + getStepDuration(dir)); Removido para usar runa enquanto corre by Yan Liima(Night)
}

void Player::onCreatureMove(const Creature* creature, const Tile* newTile, const Position& newPos,
	const Tile* oldTile, const Position& oldPos, bool teleport)
{
	Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);
	if(creature != this)
		return;

	if(getParty())
		getParty()->updateSharedExperience();

	//check if we should close trade
	if(tradeState != TRADE_TRANSFER && ((tradeItem && !Position::areInRange<1,1,0>(tradeItem->getPosition(), getPosition()))
		|| (tradePartner && !Position::areInRange<2,2,0>(tradePartner->getPosition(), getPosition()))))
		g_game.internalCloseTrade(this);

	if((teleport || oldPos.z != newPos.z) && !hasCustomFlag(PlayerCustomFlag_CanStairhop))
	{
		int32_t ticks = g_config.getNumber(ConfigManager::STAIRHOP_DELAY);
		if(ticks > 0)
		{
			addExhaust(ticks, EXHAUST_COMBAT);
			if(Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_PACIFIED, ticks))
				addCondition(condition);
		}
	}
	
	// unset editing house
	if (editHouse && !newTile->hasFlag(TILESTATE_HOUSE))
		editHouse = NULL;
}

void Player::onAddContainerItem(const Container*, const Item* item)
{
	checkTradeState(item);
}

void Player::onUpdateContainerItem(const Container* container, uint8_t slot,
	const Item* oldItem, const ItemType&, const Item* newItem, const ItemType&)
{
	if(oldItem != newItem)
		onRemoveContainerItem(container, slot, oldItem);

	if(tradeState != TRADE_TRANSFER)
		checkTradeState(oldItem);
}

void Player::onRemoveContainerItem(const Container* container, uint8_t, const Item* item)
{
	if(tradeState == TRADE_TRANSFER)
		return;

	checkTradeState(item);
	if(tradeItem)
	{
		if(tradeItem->getParent() != container && container->isHoldingItem(tradeItem))
			g_game.internalCloseTrade(this);
	}
}

void Player::onCloseContainer(const Container* container)
{
	if(!client)
		return;

	for(ContainerVector::const_iterator cl = containerVec.begin(); cl != containerVec.end(); ++cl)
	{
		if(cl->second == container) {
			client->sendCloseContainer(cl->first);
			for(AutoList<ProtocolGame>::iterator it = Player::cSpectators.begin(); it != Player::cSpectators.end(); ++it) //CAST
				if(it->second->getPlayer() == this)
					it->second->sendCloseContainer(cl->first);
		}
	}
}

void Player::onSendContainer(const Container* container)
{
	if(!client)
		return;

	bool hasParent = dynamic_cast<const Container*>(container->getParent()) != NULL;
	for(ContainerVector::const_iterator cl = containerVec.begin(); cl != containerVec.end(); ++cl)
	{
		if(cl->second == container) {
			client->sendContainer(cl->first, container, hasParent);
			for(AutoList<ProtocolGame>::iterator it = Player::cSpectators.begin(); it != Player::cSpectators.end(); ++it) //CAST
				if(it->second->getPlayer() == this)
					it->second->sendContainer(cl->first, container, hasParent);
		}
	}
}

void Player::onUpdateInventoryItem(slots_t slot, Item* oldItem, const ItemType& ,
	Item* newItem, const ItemType&)
{
	if(oldItem != newItem)
		onRemoveInventoryItem(slot, oldItem);

	if(tradeState != TRADE_TRANSFER)
		checkTradeState(oldItem);
}

void Player::onRemoveInventoryItem(slots_t, Item* item)
{
	if(tradeState == TRADE_TRANSFER)
		return;

	checkTradeState(item);
	if(tradeItem)
	{
		const Container* container = item->getContainer();
		if(container && container->isHoldingItem(tradeItem))
			g_game.internalCloseTrade(this);
	}
}

void Player::checkTradeState(const Item* item)
{
	if(!tradeItem || tradeState == TRADE_TRANSFER)
		return;

	if(tradeItem != item)
	{
		const Container* container = dynamic_cast<const Container*>(item->getParent());
		while(container != NULL)
		{
			if(container == tradeItem)
			{
				g_game.internalCloseTrade(this);
				break;
			}

			container = dynamic_cast<const Container*>(container->getParent());
		}
	}
	else
		g_game.internalCloseTrade(this);
}

void Player::setNextWalkActionTask(SchedulerTask* task)
{
	if(walkTaskEvent)
	{
		Scheduler::getInstance().stopEvent(walkTaskEvent);
		walkTaskEvent = 0;
	}

	delete walkTask;
	walkTask = task;
	setIdleTime(0);
}

void Player::setNextWalkTask(SchedulerTask* task)
{
	if(nextStepEvent)
	{
		Scheduler::getInstance().stopEvent(nextStepEvent);
		nextStepEvent = 0;
	}

	if(task)
	{
		nextStepEvent = Scheduler::getInstance().addEvent(task);
		setIdleTime(0);
	}
}

void Player::setNextActionTask(SchedulerTask* task)
{
	if(actionTaskEvent)
	{
		Scheduler::getInstance().stopEvent(actionTaskEvent);
		actionTaskEvent = 0;
	}

	if(task)
	{
		actionTaskEvent = Scheduler::getInstance().addEvent(task);
		setIdleTime(0);
	}
}

uint32_t Player::getNextActionTime(bool scheduler/* = true*/) const
{
	if(!scheduler)
		return (uint32_t)std::max((int64_t)0, ((int64_t)nextAction - OTSYS_TIME()));
	
	return (uint32_t)std::max((int64_t)SCHEDULER_MINTICKS, ((int64_t)nextAction - OTSYS_TIME()));
}

void Player::onThink(uint32_t interval)
{
	Creature::onThink(interval);
	int64_t timeNow = OTSYS_TIME();
	if(timeNow - lastPing >= 5000)
	{
		lastPing = timeNow;
		if(client) { //CAST
			for(AutoList<ProtocolGame>::iterator it = Player::cSpectators.begin(); it != Player::cSpectators.end(); ++it)
				if(it->second->getPlayer() == this)
					it->second->sendPing();

			client->sendPing();
        }
		else if(g_config.getBool(ConfigManager::STOP_ATTACK_AT_EXIT))
			setAttackedCreature(NULL);
	}

	if((timeNow - lastPong) >= 60000 && !getTile()->hasFlag(TILESTATE_NOLOGOUT)
		&& !isConnecting && !pzLocked && !hasCondition(CONDITION_INFIGHT))
	{
		if(client)
			client->logout(true, true);
		else if(g_creatureEvents->playerLogout(this, false))
			g_game.removeCreature(this, true);
	}

	messageTicks += interval;
	if(messageTicks >= 1500)
	{
		messageTicks = 0;
		addMessageBuffer();
	}
	
	if(lastMail && lastMail < (uint64_t)(OTSYS_TIME() + g_config.getNumber(ConfigManager::MAIL_ATTEMPTS_FADE)))
		mailAttempts = lastMail = 0;
}

bool Player::isMuted(uint16_t channelId, SpeakClasses type, uint32_t& time)
{
	time = 0;
	if(hasFlag(PlayerFlag_CannotBeMuted))
		return false;

	int32_t muteTicks = 0;
	for(ConditionList::iterator it = conditions.begin(); it != conditions.end(); ++it)
	{
		if((*it)->getType() == CONDITION_MUTED && (*it)->getSubId() == 0 && (*it)->getTicks() > muteTicks)
			muteTicks = (*it)->getTicks();
	}

	time = (uint32_t)muteTicks / 1000;
	return type != SPEAK_PRIVATE_PN && (type != SPEAK_CHANNEL_Y || (channelId != CHANNEL_GUILD && !g_chat.isPrivateChannel(channelId)));
}

void Player::addMessageBuffer()
{
	if(!hasFlag(PlayerFlag_CannotBeMuted) && g_config.getNumber(ConfigManager::MAX_MESSAGEBUFFER) && messageBuffer)
		messageBuffer--;
}

void Player::removeMessageBuffer()
{
	if(hasFlag(PlayerFlag_CannotBeMuted))
		return;

	int32_t maxBuffer = g_config.getNumber(ConfigManager::MAX_MESSAGEBUFFER);
	if(!maxBuffer || messageBuffer > maxBuffer + 1 || ++messageBuffer <= maxBuffer)
		return;

	uint32_t muteCount = 1;
	MuteCountMap::iterator it = muteCountMap.find(guid);
	if(it != muteCountMap.end())
		muteCount = it->second;

	uint32_t muteTime = 5 * muteCount * muteCount;
	muteCountMap[guid] = muteCount + 1;
	if(Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_MUTED, muteTime * 1000))
		addCondition(condition);

	char buffer[50];
	sprintf(buffer, "You are muted for %d seconds.", muteTime);
	sendTextMessage(MSG_STATUS_SMALL, buffer);
}

void Player::drainHealth(Creature* attacker, CombatType_t combatType, int32_t damage)
{
	Creature::drainHealth(attacker, combatType, damage);
	char buffer[150];
	if(attacker)
		sprintf(buffer, "You lose %d hitpoint%s due to an attack by %s.", damage, (damage != 1 ? "s" : ""), attacker->getNameDescription().c_str());
	else
		sprintf(buffer, "You lose %d hitpoint%s.", damage, (damage != 1 ? "s" : ""));

	sendStats();
	sendTextMessage(MSG_EVENT_DEFAULT, buffer);
}

void Player::drainMana(Creature* attacker, CombatType_t combatType, int32_t damage)
{
	Creature::drainMana(attacker, combatType, damage);
	char buffer[150];
	if(attacker)
		sprintf(buffer, "You lose %d mana blocking an attack by %s.", damage, attacker->getNameDescription().c_str());
	else
		sprintf(buffer, "You lose %d mana.", damage);

	sendStats();
	sendTextMessage(MSG_EVENT_DEFAULT, buffer);
}

void Player::addManaSpent(uint64_t amount, bool useMultiplier/* = true*/)
{
	if(!amount)
		return;

	uint64_t currReqMana = vocation->getReqMana(magLevel), nextReqMana = vocation->getReqMana(magLevel + 1);
	if(currReqMana > nextReqMana) //player has reached max magic level
		return;

	if(useMultiplier)
		amount = uint64_t((double)amount * rates[SKILL__MAGLEVEL] * g_config.getDouble(ConfigManager::RATE_MAGIC));

	bool advance = false;
	while(manaSpent + amount >= nextReqMana)
	{
		amount -= nextReqMana - manaSpent;
		manaSpent = 0;
		magLevel++;

		char advMsg[50];
		sprintf(advMsg, "You advanced to magic level %d.", magLevel);
		sendTextMessage(MSG_EVENT_ADVANCE, advMsg);

		advance = true;
		CreatureEventList advanceEvents = getCreatureEvents(CREATURE_EVENT_ADVANCE);
		for(CreatureEventList::iterator it = advanceEvents.begin(); it != advanceEvents.end(); ++it)
			(*it)->executeAdvance(this, SKILL__MAGLEVEL, (magLevel - 1), magLevel);

		currReqMana = nextReqMana;
		nextReqMana = vocation->getReqMana(magLevel + 1);
		if(currReqMana > nextReqMana)
		{
			amount = 0;
			break;
		}
	}

	if(amount)
		manaSpent += amount;

	uint16_t newPercent = Player::getPercentLevel(manaSpent, nextReqMana);
	if(magLevelPercent != newPercent)
	{
		magLevelPercent = newPercent;
		sendStats();
	}
	else if(advance)
		sendStats();
}

void Player::setSkillLevel(skills_t skill, uint32_t value)
{
	uint32_t old_level = skills[skill][SKILL_LEVEL];
	std::stringstream s;

	skills[skill][SKILL_LEVEL] = value;
	skills[skill][SKILL_TRIES] = 0;
	skills[skill][SKILL_PERCENT] = 0;

	s.str("");
	s << "You advanced in " << getSkillName(skill);
	if(g_config.getBool(ConfigManager::ADVANCING_SKILL_LEVEL))
		s << " [" << skills[skill][SKILL_LEVEL] << "]";

	s << ".";
	sendTextMessage(MSG_EVENT_ADVANCE, s.str().c_str());

	CreatureEventList advanceEvents = getCreatureEvents(CREATURE_EVENT_ADVANCE);
	for(CreatureEventList::iterator it = advanceEvents.begin(); it != advanceEvents.end(); ++it)
		(*it)->executeAdvance(this, skill, old_level, skills[skill][SKILL_LEVEL]);

	sendSkills();
}

void Player::setMagicLevel(uint64_t value)
{
	uint64_t old_level = magLevel;

	magLevel = value;
	manaSpent = 0;
	magLevelPercent = 0;
	
	char advMsg[50];
	sprintf(advMsg, "You advanced to magic level %d.", magLevel);
	sendTextMessage(MSG_EVENT_ADVANCE, advMsg);

	CreatureEventList advanceEvents = getCreatureEvents(CREATURE_EVENT_ADVANCE);
	for(CreatureEventList::iterator it = advanceEvents.begin(); it != advanceEvents.end(); ++it)
		(*it)->executeAdvance(this, SKILL__MAGLEVEL, old_level, magLevel);

	sendStats();
}

void Player::addExperience(uint64_t exp)
{
	uint32_t prevLevel = level;
	uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
	if(Player::getExpForLevel(level) > nextLevelExp)
	{
		//player has reached max level
		levelPercent = 0;
		sendStats();
		return;
	}

	experience += exp;
	while(experience >= nextLevelExp)
	{
		healthMax += vocation->getGain(GAIN_HEALTH);
		health += vocation->getGain(GAIN_HEALTH);
		manaMax += vocation->getGain(GAIN_MANA);
		mana += vocation->getGain(GAIN_MANA);
		capacity += vocation->getGainCap();

		++level;
		nextLevelExp = Player::getExpForLevel(level + 1);
		if(Player::getExpForLevel(level) > nextLevelExp) //player has reached max level
			break;
	}

	if(prevLevel != level)
	{
		updateBaseSpeed();
		g_game.changeSpeed(this, 0);

		g_game.addCreatureHealth(this);
		if(party)
			party->updateSharedExperience();

		char advMsg[60];
		sprintf(advMsg, "You advanced from Level %d to Level %d.", prevLevel, level);
		sendTextMessage(MSG_EVENT_ADVANCE, advMsg);

		CreatureEventList advanceEvents = getCreatureEvents(CREATURE_EVENT_ADVANCE);
		for(CreatureEventList::iterator it = advanceEvents.begin(); it != advanceEvents.end(); ++it)
			(*it)->executeAdvance(this, SKILL__LEVEL, prevLevel, level);
	}

	uint64_t currLevelExp = Player::getExpForLevel(level);
	nextLevelExp = Player::getExpForLevel(level + 1);
	levelPercent = 0;
	if(nextLevelExp > currLevelExp)
		levelPercent = Player::getPercentLevel(experience - currLevelExp, nextLevelExp - currLevelExp);

	sendStats();
}

void Player::removeExperience(uint64_t exp, bool updateStats/* = true*/)
{
	uint32_t prevLevel = level;
	experience -= std::min(exp, experience);
	while(level > 1 && experience < Player::getExpForLevel(level))
	{
		level--;
		healthMax = std::max((int32_t)0, (healthMax - (int32_t)vocation->getGain(GAIN_HEALTH)));
		manaMax = std::max((int32_t)0, (manaMax - (int32_t)vocation->getGain(GAIN_MANA)));
		capacity = std::max((double)0, (capacity - (double)vocation->getGainCap()));
	}

	if(prevLevel != level)
	{
		if(updateStats)
		{
			updateBaseSpeed();
			g_game.changeSpeed(this, 0);
			g_game.addCreatureHealth(this);
		}

		char advMsg[90];
		sprintf(advMsg, "You were downgraded from Level %d to Level %d.", prevLevel, level);
		sendTextMessage(MSG_EVENT_ADVANCE, advMsg);
	}

	uint64_t currLevelExp = Player::getExpForLevel(level),
		nextLevelExp = Player::getExpForLevel(level + 1);
	if(nextLevelExp > currLevelExp)
		levelPercent = Player::getPercentLevel(experience - currLevelExp, nextLevelExp - currLevelExp);
	else
		levelPercent = 0;

	if(updateStats)
		sendStats();
}

uint16_t Player::getPercentLevel(uint64_t count, uint64_t nextLevelCount)
{
	if(nextLevelCount > 0)
		return std::min((uint32_t)100, std::max((uint32_t)0, uint32_t(count * 100 / nextLevelCount)));

	return 0;
}

void Player::onBlockHit(BlockType_t)
{
	if(shieldBlockCount > 0)
	{
		--shieldBlockCount;
		if(hasShield())
			addSkillAdvance(SKILL_SHIELD, 1);
	}
}

void Player::onAttackedCreatureBlockHit(Creature* target, BlockType_t blockType)
{
	Creature::onAttackedCreatureBlockHit(target, blockType);
	lastAttackBlockType = blockType;
	switch(blockType)
	{
		case BLOCK_NONE:
		{
			bloodHitCount = shieldBlockCount = 30;
			addAttackSkillPoint = true;
			break;
		}

		case BLOCK_DEFENSE:
		case BLOCK_ARMOR:
		{
			//need to draw blood every 30 hits
			if(bloodHitCount > 0)
			{
				addAttackSkillPoint = true;
				--bloodHitCount;
			}
			else
				addAttackSkillPoint = false;

			break;
		}

		default:
		{
			addAttackSkillPoint = false;
			break;
		}
	}
}

bool Player::hasShield() const
{
	Item* item = getInventoryItem(SLOT_LEFT);
	return (item && item->getWeaponType() == WEAPON_SHIELD) || ((item = getInventoryItem(SLOT_RIGHT)) && item->getWeaponType() == WEAPON_SHIELD);
}

BlockType_t Player::blockHit(Creature* attacker, CombatType_t combatType, int32_t& damage,
	bool checkDefense/* = false*/, bool checkArmor/* = false*/, bool reflect/* = true*/)
{
	BlockType_t blockType = Creature::blockHit(attacker, combatType, damage, checkDefense, checkArmor);
	if(attacker)
	{
		int16_t color = g_config.getNumber(ConfigManager::SQUARE_COLOR);
		if(color < 0)
			color = random_range(0, 254);

		sendCreatureSquare(attacker, color);
	}

	if(blockType != BLOCK_NONE)
		return blockType;

	if(vocation->getMultiplier(MULTIPLIER_MAGICDEFENSE) != 1.0 && combatType != COMBAT_NONE &&
		combatType != COMBAT_PHYSICALDAMAGE && combatType != COMBAT_UNDEFINEDDAMAGE &&
		combatType != COMBAT_DROWNDAMAGE)
		damage -= (int32_t)std::ceil((double)(damage * vocation->getMultiplier(MULTIPLIER_MAGICDEFENSE)) / 100.);

	if(damage <= 0)
		return blockType;

	int32_t blocked = 0, reflected = 0;
	if(reflect)
		reflect = attacker && !attacker->isRemoved() && attacker->getHealth() > 0;

	Item* item = NULL;
	for(int32_t slot = SLOT_FIRST; slot < SLOT_LAST; ++slot)
	{
		if(!(item = getInventoryItem((slots_t)slot)) || item->isRemoved() ||
			(g_moveEvents->hasEquipEvent(item) && !isItemAbilityEnabled((slots_t)slot)))
			continue;

		const ItemType& it = Item::items[item->getID()];
		if(it.abilities.absorb[combatType])
		{
			blocked += (int32_t)std::ceil((double)(damage * it.abilities.absorb[combatType]) / 100.);
			if(item->hasCharges())
				g_game.transformItem(item, item->getID(), std::max((int32_t)0, (int32_t)item->getCharges() - 1));
		}

		if(!reflect)
			continue;

		if(it.abilities.reflect[REFLECT_PERCENT][combatType] && random_range(1, 100) < it.abilities.reflect[REFLECT_CHANCE][combatType])
		{
			reflected += (int32_t)std::ceil((double)(damage * it.abilities.reflect[REFLECT_PERCENT][combatType]) / 100.);
			if(item->hasCharges() && !it.abilities.absorb[combatType])
				g_game.transformItem(item, item->getID(), std::max((int32_t)0, (int32_t)item->getCharges() - 1));
		}
	}

	if(outfitAttributes)
	{
		uint32_t tmp = Outfits::getInstance()->getOutfitAbsorb(defaultOutfit.lookType, sex, combatType);
		if(tmp)
			blocked += (int32_t)std::ceil((double)(damage * tmp) / 100.);

		if(reflect)
		{
			tmp = Outfits::getInstance()->getOutfitReflect(defaultOutfit.lookType, sex, combatType);
			if(tmp)
				reflected += (int32_t)std::ceil((double)(damage * tmp) / 100.);
		}
	}

	if(vocation->getAbsorb(combatType))
		blocked += (int32_t)std::ceil((double)(damage * vocation->getAbsorb(combatType)) / 100.);

	if(reflect && vocation->getReflect(combatType))
		reflected += (int32_t)std::ceil((double)(damage * vocation->getReflect(combatType)) / 100.);

	damage -= blocked;
	if(damage <= 0)
	{
		damage = 0;
		blockType = BLOCK_DEFENSE;
	}

	if(reflected)
	{
		if(combatType != COMBAT_HEALING)
			reflected = -reflected;

		if(attacker->blockHit(this, combatType, reflected, false, false, false) == BLOCK_NONE)
			g_game.combatChangeHealth(combatType, NULL, attacker, reflected);
	}

	return blockType;
}

uint32_t Player::getIP() const
{
	if(client)
		return client->getIP();

	return lastIP;
}

bool Player::onDeath()
{
	kickCastViewers(); //CAST
     
	Item* preventLoss = NULL;
	Item* preventDrop = NULL;
	if(getZone() == ZONE_HARDCORE)
	{
		setDropLoot(LOOT_DROP_NONE);
		setLossSkill(false);
	}
	else if(skull < SKULL_RED)
	{
		Item* item = NULL;
		for(int32_t i = SLOT_FIRST; ((skillLoss || lootDrop == LOOT_DROP_FULL) && i < SLOT_LAST); ++i)
		{
			if(!(item = getInventoryItem((slots_t)i)) || item->isRemoved() ||
				(g_moveEvents->hasEquipEvent(item) && !isItemAbilityEnabled((slots_t)i)))
				continue;

			const ItemType& it = Item::items[item->getID()];
			if(lootDrop == LOOT_DROP_FULL && it.abilities.preventDrop)
			{
				setDropLoot(LOOT_DROP_PREVENT);
				preventDrop = item;
			}

			if(skillLoss && !preventLoss && it.abilities.preventLoss)
				preventLoss = item;
		}
	}

	if(!Creature::onDeath())
	{
		if(preventDrop)
			setDropLoot(LOOT_DROP_FULL);

		return false;
	}

	if(preventLoss)
	{
		setLossSkill(false);
		if(preventLoss->getCharges() > 1) //weird, but transform failed to remove for some hosters
			g_game.transformItem(preventLoss, preventLoss->getID(), std::max(0, ((int32_t)preventLoss->getCharges() - 1)));
		else
			g_game.internalRemoveItem(NULL, preventDrop);
	}

	if(preventDrop && preventDrop != preventLoss)
	{
		if(preventDrop->getCharges() > 1) //weird, but transform failed to remove for some hosters
			g_game.transformItem(preventDrop, preventDrop->getID(), std::max(0, ((int32_t)preventDrop->getCharges() - 1)));
		else
			g_game.internalRemoveItem(NULL, preventDrop);
	}

	removeConditions(CONDITIONEND_DEATH);
	if(skillLoss)
	{
		uint64_t lossExperience = getLostExperience();
		removeExperience(lossExperience, false);
		double percent = 1. - ((double)(experience - lossExperience) / experience);

		//Magic level loss
		uint64_t sumMana = 0, lostMana = 0;
		for(uint32_t i = 1; i <= magLevel; ++i)
			sumMana += vocation->getReqMana(i);

		sumMana += manaSpent;
		lostMana = (uint64_t)std::ceil(sumMana * ((double)(percent * lossPercent[LOSS_MANA]) / 100.));
		while(lostMana > manaSpent && magLevel > 0)
		{
			lostMana -= manaSpent;
			manaSpent = vocation->getReqMana(magLevel);
			magLevel--;
		}

		manaSpent -= std::max((int32_t)0, (int32_t)lostMana);
		uint64_t nextReqMana = vocation->getReqMana(magLevel + 1);
		if(nextReqMana > vocation->getReqMana(magLevel))
			magLevelPercent = Player::getPercentLevel(manaSpent, nextReqMana);
		else
			magLevelPercent = 0;

		//Skill loss
		uint64_t lostSkillTries, sumSkillTries;
		for(int16_t i = 0; i < 7; ++i) //for each skill
		{
			lostSkillTries = sumSkillTries = 0;
			for(uint32_t c = 11; c <= skills[i][SKILL_LEVEL]; ++c) //sum up all required tries for all skill levels
				sumSkillTries += vocation->getReqSkillTries(i, c);

			sumSkillTries += skills[i][SKILL_TRIES];
			lostSkillTries = (uint64_t)std::ceil(sumSkillTries * ((double)(percent * lossPercent[LOSS_SKILLS]) / 100.));
			while(lostSkillTries > skills[i][SKILL_TRIES])
			{
				lostSkillTries -= skills[i][SKILL_TRIES];
				skills[i][SKILL_TRIES] = vocation->getReqSkillTries(i, skills[i][SKILL_LEVEL]);
				if(skills[i][SKILL_LEVEL] < 11)
				{
					skills[i][SKILL_LEVEL] = 10;
					skills[i][SKILL_TRIES] = lostSkillTries = 0;
					break;
				}
				else
					skills[i][SKILL_LEVEL]--;
			}

			skills[i][SKILL_TRIES] = std::max((int32_t)0, (int32_t)(skills[i][SKILL_TRIES] - lostSkillTries));
		}

		blessings = 0;
		loginPosition = masterPosition;
		if(!inventory[SLOT_BACKPACK])
			__internalAddThing(SLOT_BACKPACK, Item::CreateItem(g_config.getNumber(ConfigManager::DEATH_CONTAINER)));

		sendIcons();
		sendStats();
		sendSkills();

		g_creatureEvents->playerLogout(this, true);
		g_game.removeCreature(this, false);
		sendReLoginWindow();
	}
	else
	{
		setLossSkill(true);
		if(preventLoss)
		{
			loginPosition = masterPosition;
			g_creatureEvents->playerLogout(this, true);
			g_game.removeCreature(this, false);
			sendReLoginWindow();
		}
	}

	return true;
}

void Player::dropCorpse(DeathList deathList)
{
	if(lootDrop == LOOT_DROP_NONE)
	{
		pzLocked = false;
		if(health <= 0)
		{
			health = healthMax;
			mana = manaMax;
		}

		setDropLoot(LOOT_DROP_FULL);
		sendStats();
		sendIcons();

		onIdleStatus();
		g_game.addCreatureHealth(this);
		g_game.internalTeleport(this, masterPosition, false);
	}
	else
	{
		Creature::dropCorpse(deathList);
		if(g_config.getBool(ConfigManager::DEATH_LIST))
			IOLoginData::getInstance()->playerDeath(this, deathList);
	}
}

Item* Player::createCorpse(DeathList deathList)
{
	Item* corpse = Creature::createCorpse(deathList);
	if(!corpse)
		return NULL;

	std::stringstream ss;
	ss << "You recognize " << getNameDescription() << ". " << (sex % 2 ? "He" : "She") << " was killed by ";
	if(deathList[0].isCreatureKill())
	{
		ss << deathList[0].getKillerCreature()->getNameDescription();
		if(deathList[0].getKillerCreature()->getMaster())
			ss << " summoned by " << deathList[0].getKillerCreature()->getMaster()->getNameDescription();
	}
	else
		ss << deathList[0].getKillerName();

	if(deathList.size() > 1)
	{
		if(deathList[0].getKillerType() != deathList[1].getKillerType())
		{
			if(deathList[1].isCreatureKill())
			{
				ss << " and by " << deathList[1].getKillerCreature()->getNameDescription();
				if(deathList[1].getKillerCreature()->getMaster())
					ss << " summoned by " << deathList[1].getKillerCreature()->getMaster()->getNameDescription();
			}
			else
				ss << " and by " << deathList[1].getKillerName();
		}
		else if(deathList[1].isCreatureKill())
		{
			if(deathList[0].getKillerCreature()->getName() != deathList[1].getKillerCreature()->getName())
			{
				ss << " and by " << deathList[1].getKillerCreature()->getNameDescription();
				if(deathList[1].getKillerCreature()->getMaster())
					ss << " summoned by " << deathList[1].getKillerCreature()->getMaster()->getNameDescription();
			}
		}
		else if(asLowerCaseString(deathList[0].getKillerName()) != asLowerCaseString(deathList[1].getKillerName()))
			ss << " and by " << deathList[1].getKillerName();
	}

	ss << ".";
	corpse->setSpecialDescription(ss.str().c_str());
	return corpse;
}

void Player::addExhaust(uint32_t ticks, Exhaust_t type)
{
	if(Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT,
		CONDITION_EXHAUST, ticks, 0, false, (uint32_t)type))
		addCondition(condition);
}

void Player::addInFightTicks(bool pzLock, int32_t ticks/* = 0*/)
{
	if(hasFlag(PlayerFlag_NotGainInFight))
		return;

	if(!ticks)
		ticks = g_config.getNumber(ConfigManager::PZ_LOCKED);
	else
		ticks = std::max(-1, ticks);

	if(pzLock)
		pzLocked = true;

	if(Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT,
		CONDITION_INFIGHT, ticks))
		addCondition(condition);
}

void Player::addDefaultRegeneration(uint32_t addTicks)
{
	Condition* condition = getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT);
	if(condition)
		condition->setTicks(condition->getTicks() + addTicks);
	else if((condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_REGENERATION, addTicks)))
	{
		condition->setParam(CONDITIONPARAM_HEALTHGAIN, vocation->getGainAmount(GAIN_HEALTH));
		condition->setParam(CONDITIONPARAM_HEALTHTICKS, vocation->getGainTicks(GAIN_HEALTH) * 1000);
		condition->setParam(CONDITIONPARAM_MANAGAIN, vocation->getGainAmount(GAIN_MANA));
		condition->setParam(CONDITIONPARAM_MANATICKS, vocation->getGainTicks(GAIN_MANA) * 1000);
		addCondition(condition);
	}
}

void Player::removeList()
{
	Manager::getInstance()->removeUser(id);
	autoList.erase(id);
	castAutoList.erase(id); //CAST
	
	if(!isGhost())
	{
		for(AutoList<Player>::iterator it = autoList.begin(); it != autoList.end(); ++it)
			it->second->notifyLogOut(this);
	}
	else
	{
		for(AutoList<Player>::iterator it = autoList.begin(); it != autoList.end(); ++it)
		{
			if(it->second->canSeeCreature(this))
				it->second->notifyLogOut(this);
		}
	}
}

void Player::addList()
{
	if(!isGhost())
	{
		for(AutoList<Player>::iterator it = autoList.begin(); it != autoList.end(); ++it)
			it->second->notifyLogIn(this);
	}
	else
	{
		for(AutoList<Player>::iterator it = autoList.begin(); it != autoList.end(); ++it)
		{
			if(it->second->canSeeCreature(this))
				it->second->notifyLogIn(this);
		}
	}

	autoList[id] = this;
	Manager::getInstance()->addUser(this);
}

void Player::kick(bool displayEffect, bool forceLogout)
{
	if(!client)
	{
		if(g_creatureEvents->playerLogout(this, forceLogout))
			g_game.removeCreature(this);
	}
	else
		client->logout(displayEffect, forceLogout);
}

void Player::notifyLogIn(Player* loginPlayer)
{
	if(!client)
		return;

	VIPSet::iterator it = VIPList.find(loginPlayer->getGUID());
	if(it != VIPList.end())
		client->sendVIPLogIn(loginPlayer->getGUID());
}

void Player::notifyLogOut(Player* logoutPlayer)
{
	if(!client)
		return;

	VIPSet::iterator it = VIPList.find(logoutPlayer->getGUID());
	if(it != VIPList.end())
		client->sendVIPLogOut(logoutPlayer->getGUID());
}

bool Player::removeVIP(uint32_t _guid)
{
	VIPSet::iterator it = VIPList.find(_guid);
	if(it == VIPList.end())
		return false;

	VIPList.erase(it);
	return true;
}

bool Player::addVIP(uint32_t _guid, std::string& name, bool isOnline, bool internal/* = false*/)
{
	if(guid == _guid)
	{
		if(!internal)
			sendTextMessage(MSG_STATUS_SMALL, "You cannot add yourself.");

		return false;
	}

	if(VIPList.size() > (group ? group->getMaxVips(isPremium()) : g_config.getNumber(ConfigManager::VIPLIST_DEFAULT_LIMIT)))
	{
		if(!internal)
			sendTextMessage(MSG_STATUS_SMALL, "You cannot add more buddies.");

		return false;
	}

	VIPSet::iterator it = VIPList.find(_guid);
	if(it != VIPList.end())
	{
		if(!internal)
			sendTextMessage(MSG_STATUS_SMALL, "This player is already in your list.");

		return false;
	}

	VIPList.insert(_guid);
	if(client && !internal)
		client->sendVIP(_guid, name, isOnline);

	return true;
}

//close container and its child containers
void Player::autoCloseContainers(const Container* container)
{
	typedef std::vector<uint32_t> CloseList;
	CloseList closeList;
	for(ContainerVector::iterator it = containerVec.begin(); it != containerVec.end(); ++it)
	{
		Container* tmp = it->second;
		while(tmp != NULL)
		{
			if(tmp->isRemoved() || tmp == container)
			{
				closeList.push_back(it->first);
				break;
			}

			tmp = dynamic_cast<Container*>(tmp->getParent());
		}
	}

	for(CloseList::iterator it = closeList.begin(); it != closeList.end(); ++it)
	{
		closeContainer(*it);
		if(client) {
			client->sendCloseContainer(*it);
			for(AutoList<ProtocolGame>::iterator it2 = Player::cSpectators.begin(); it2 != Player::cSpectators.end(); ++it2) //CAST
				if(it2->second->getPlayer() == this)
					it2->second->sendCloseContainer(*it);
		}
	}
}

bool Player::hasCapacity(const Item* item, uint32_t count) const
{
	if(hasFlag(PlayerFlag_CannotPickupItem))
		return false;

	if(hasFlag(PlayerFlag_HasInfiniteCapacity) || item->getTopParent() == this)
		return true;

	double itemWeight = 0;
	if(item->isStackable())
		itemWeight = Item::items[item->getID()].weight * count;
	else
		itemWeight = item->getWeight();

	return (itemWeight < getFreeCapacity());
}

ReturnValue Player::__queryAdd(int32_t index, const Thing* thing, uint32_t count, uint32_t flags) const
{
	const Item* item = thing->getItem();
	if(!item)
		return RET_NOTPOSSIBLE;

	bool childOwner = ((flags & FLAG_CHILDISOWNER) == FLAG_CHILDISOWNER), skipLimit = ((flags & FLAG_NOLIMIT) == FLAG_NOLIMIT);
	if(childOwner)
	{
		//a child container is querying the player, just check if enough capacity
		if(skipLimit || hasCapacity(item, count))
			return RET_NOERROR;

		return RET_NOTENOUGHCAPACITY;
	}

	if(!item->isPickupable())
		return RET_CANNOTPICKUP;

	ReturnValue ret = RET_NOERROR;
	if((item->getSlotPosition() & SLOTP_HEAD) || (item->getSlotPosition() & SLOTP_NECKLACE) ||
		(item->getSlotPosition() & SLOTP_BACKPACK) || (item->getSlotPosition() & SLOTP_ARMOR) ||
		(item->getSlotPosition() & SLOTP_LEGS) || (item->getSlotPosition() & SLOTP_FEET) ||
		(item->getSlotPosition() & SLOTP_RING))
		ret = RET_CANNOTBEDRESSED;
	else if(item->getSlotPosition() & SLOTP_TWO_HAND)
		ret = RET_PUTTHISOBJECTINBOTHHANDS;
	else if((item->getSlotPosition() & SLOTP_RIGHT) || (item->getSlotPosition() & SLOTP_LEFT))
	{
		if(!g_config.getBool(ConfigManager::CLASSIC_EQUIPMENT_SLOTS))
			ret = RET_CANNOTBEDRESSED;
		else
			ret = RET_PUTTHISOBJECTINYOURHAND;
	}

	switch(index)
	{
		case SLOT_HEAD:
			if(item->getSlotPosition() & SLOTP_HEAD)
				ret = RET_NOERROR;
			break;
		case SLOT_NECKLACE:
			if(item->getSlotPosition() & SLOTP_NECKLACE)
				ret = RET_NOERROR;
			break;
		case SLOT_BACKPACK:
			if(item->getSlotPosition() & SLOTP_BACKPACK)
				ret = RET_NOERROR;
			break;
		case SLOT_ARMOR:
			if(item->getSlotPosition() & SLOTP_ARMOR)
				ret = RET_NOERROR;
			break;
		case SLOT_RIGHT:
			if(item->getSlotPosition() & SLOTP_RIGHT)
			{
				if(!g_config.getBool(ConfigManager::CLASSIC_EQUIPMENT_SLOTS))
				{
					if(!item->isWeapon() || (item->getWeaponType() != WEAPON_SHIELD && !item->isDualWield()))
						ret = RET_CANNOTBEDRESSED;
					else
					{
						const Item* leftItem = inventory[SLOT_LEFT];
						if(leftItem)
						{
							if((leftItem->getSlotPosition() | item->getSlotPosition()) & SLOTP_TWO_HAND)
								ret = RET_BOTHHANDSNEEDTOBEFREE;
							else
								ret = RET_NOERROR;
						}
						else
							ret = RET_NOERROR;
					}
				}
				//check if we already carry an item in the other hand
				else if(item->getSlotPosition() & SLOTP_TWO_HAND)
				{
					if(inventory[SLOT_LEFT] && inventory[SLOT_LEFT] != item)
						ret = RET_BOTHHANDSNEEDTOBEFREE;
					else
						ret = RET_NOERROR;
				}
				else if(inventory[SLOT_LEFT])
				{
					const Item* leftItem = inventory[SLOT_LEFT];
					WeaponType_t type = item->getWeaponType(), leftType = leftItem->getWeaponType();
					if(leftItem->getSlotPosition() & SLOTP_TWO_HAND)
						ret = RET_DROPTWOHANDEDITEM;
					else if(item == leftItem && item->getItemCount() == count)
						ret = RET_NOERROR;
					else if(leftType == WEAPON_SHIELD && type == WEAPON_SHIELD)
						ret = RET_CANONLYUSEONESHIELD;
					else if(!leftItem->isWeapon() || !item->isWeapon() ||
						leftType == WEAPON_AMMO || type == WEAPON_AMMO ||
						leftType == WEAPON_SHIELD || type == WEAPON_SHIELD ||
						(leftItem->isDualWield() && item->isDualWield()))
						ret = RET_NOERROR;
					else
						ret = RET_CANONLYUSEONEWEAPON;
				}
				else
					ret = RET_NOERROR;
			}
			break;
		case SLOT_LEFT:
			if(item->getSlotPosition() & SLOTP_LEFT)
			{
				if(!g_config.getBool(ConfigManager::CLASSIC_EQUIPMENT_SLOTS))
				{
					if(!item->isWeapon() || item->getWeaponType() == WEAPON_SHIELD)
						ret = RET_CANNOTBEDRESSED;
					else if(inventory[SLOT_RIGHT] && (item->getSlotPosition() & SLOTP_TWO_HAND))
						ret = RET_BOTHHANDSNEEDTOBEFREE;
					else
						ret = RET_NOERROR;
				}
				//check if we already carry an item in the other hand
				else if(item->getSlotPosition() & SLOTP_TWO_HAND)
				{
					if(inventory[SLOT_RIGHT] && inventory[SLOT_RIGHT] != item)
						ret = RET_BOTHHANDSNEEDTOBEFREE;
					else
						ret = RET_NOERROR;
				}
				else if(inventory[SLOT_RIGHT])
				{
					const Item* rightItem = inventory[SLOT_RIGHT];
					WeaponType_t type = item->getWeaponType(), rightType = rightItem->getWeaponType();
					if(rightItem->getSlotPosition() & SLOTP_TWO_HAND)
						ret = RET_DROPTWOHANDEDITEM;
					else if(item == rightItem && item->getItemCount() == count)
						ret = RET_NOERROR;
					else if(rightType == WEAPON_SHIELD && type == WEAPON_SHIELD)
						ret = RET_CANONLYUSEONESHIELD;
					else if(!rightItem->isWeapon() || !item->isWeapon() ||
						rightType == WEAPON_AMMO || type == WEAPON_AMMO ||
						rightType == WEAPON_SHIELD || type == WEAPON_SHIELD ||
						(rightItem->isDualWield() && item->isDualWield()))
						ret = RET_NOERROR;
					else
						ret = RET_CANONLYUSEONEWEAPON;
				}
				else
					ret = RET_NOERROR;
			}
			break;
		case SLOT_LEGS:
			if(item->getSlotPosition() & SLOTP_LEGS)
				ret = RET_NOERROR;
			break;
		case SLOT_FEET:
			if(item->getSlotPosition() & SLOTP_FEET)
				ret = RET_NOERROR;
			break;
		case SLOT_RING:
			if(item->getSlotPosition() & SLOTP_RING)
				ret = RET_NOERROR;
			break;
		case SLOT_AMMO:
			if((item->getSlotPosition() & SLOTP_AMMO) || g_config.getBool(ConfigManager::CLASSIC_EQUIPMENT_SLOTS))
				ret = RET_NOERROR;
			break;
		case SLOT_WHEREEVER:
		case -1:
			ret = RET_NOTENOUGHROOM;
			break;
		default:
			ret = RET_NOTPOSSIBLE;
			break;
	}

    Player* self = const_cast<Player*>(this);
	if(ret == RET_NOERROR || ret == RET_NOTENOUGHROOM)
	{
		//need an exchange with source?
		Item* tmpItem = NULL;
		if((tmpItem = getInventoryItem((slots_t)index)) && (!tmpItem->isStackable() || tmpItem->getID() != item->getID()))
			return RET_NEEDEXCHANGE;

		if(!g_moveEvents->onPlayerEquip(self, const_cast<Item*>(item), (slots_t)index, true))
			return RET_CANNOTBEDRESSED;

		//check if enough capacity
		if(!hasCapacity(item, count))
			return RET_NOTENOUGHCAPACITY;
	}

    if(index == SLOT_LEFT || index == SLOT_RIGHT)
	{
		if(ret == RET_NOERROR && item->getWeaponType() != WEAPON_NONE)
			self->setLastAttack(OTSYS_TIME());

		Item* tmpItem = inventory[(slots_t)index];
		if(ret == RET_NOTENOUGHROOM && g_game.internalAddItem(NULL, self, tmpItem, INDEX_WHEREEVER) == RET_NOERROR)
		{
			self->sendRemoveInventoryItem((slots_t)index, tmpItem);
			self->onRemoveInventoryItem((slots_t)index, tmpItem);

			self->inventory[(slots_t)index] = NULL;
			self->updateWeapon();
			self->inventoryWeight -= tmpItem->getWeight();
			self->sendStats();
		}
	}
	
	return ret;
}

ReturnValue Player::__queryMaxCount(int32_t index, const Thing* thing, uint32_t count, uint32_t& maxQueryCount,
	uint32_t flags) const
{
	const Item* item = thing->getItem();
	if(!item)
	{
		maxQueryCount = 0;
		return RET_NOTPOSSIBLE;
	}

	if(index == INDEX_WHEREEVER)
	{
		uint32_t n = 0;
		for(int32_t i = SLOT_FIRST; i < SLOT_LAST; ++i)
		{
			if(Item* inventoryItem = inventory[i])
			{
				if(Container* subContainer = inventoryItem->getContainer())
				{
					uint32_t queryCount = 0;
					subContainer->__queryMaxCount(INDEX_WHEREEVER, item, item->getItemCount(), queryCount, flags);

					//iterate through all items, including sub-containers (deep search)
					n += queryCount;
					for(ContainerIterator cit = subContainer->begin(); cit != subContainer->end(); ++cit)
					{
						if(Container* tmpContainer  = (*cit)->getContainer())
						{
							queryCount = 0;
							tmpContainer->__queryMaxCount(INDEX_WHEREEVER, item, item->getItemCount(), queryCount, flags);
							n += queryCount;
						}
					}
				}
				else if(inventoryItem->isStackable() && item->getID() == inventoryItem->getID() && inventoryItem->getItemCount() < 100)
				{
					uint32_t remainder = (100 - inventoryItem->getItemCount());
					if(__queryAdd(i, item, remainder, flags) == RET_NOERROR)
						n += remainder;
				}
			}
			else if(__queryAdd(i, item, item->getItemCount(), flags) == RET_NOERROR)
			{
				if(item->isStackable())
					n += 100;
				else
					n += 1;
			}
		}

		maxQueryCount = n;
	}
	else
	{
		const Thing* destThing = __getThing(index);
		const Item* destItem = NULL;
		if(destThing)
			destItem = destThing->getItem();

		if(destItem)
		{
			if(destItem->isStackable() && item->getID() == destItem->getID() && destItem->getItemCount() < 100)
				maxQueryCount = 100 - destItem->getItemCount();
			else
				maxQueryCount = 0;
		}
		else if(__queryAdd(index, item, count, flags) == RET_NOERROR)
		{
			if(item->isStackable())
				maxQueryCount = 100;
			else
				maxQueryCount = 1;

			return RET_NOERROR;
		}
	}

	if(maxQueryCount < count)
		return RET_NOTENOUGHROOM;

	return RET_NOERROR;
}

ReturnValue Player::__queryRemove(const Thing* thing, uint32_t count, uint32_t flags) const
{
	int32_t index = __getIndexOfThing(thing);
	if(index == -1)
		return RET_NOTPOSSIBLE;

	const Item* item = thing->getItem();
	if(!item)
		return RET_NOTPOSSIBLE;

	if(!count || (item->isStackable() && count > item->getItemCount()))
		return RET_NOTPOSSIBLE;

	 if(!item->isMovable() && !hasBitSet(FLAG_IGNORENOTMOVABLE, flags))
		return RET_NOTMOVABLE;

	return RET_NOERROR;
}

Cylinder* Player::__queryDestination(int32_t& index, const Thing* thing, Item** destItem,
	uint32_t& flags)
{
	if(!index /*drop to capacity window*/ || index == INDEX_WHEREEVER)
	{
		*destItem = NULL;
		const Item* item = thing->getItem();
		if(!item)
			return this;

		std::list<std::pair<Container*, int32_t> > containers;
		std::list<std::pair<Cylinder*, int32_t> > freeSlots;

		bool autoStack = !((flags & FLAG_IGNOREAUTOSTACK) == FLAG_IGNOREAUTOSTACK);
		for(int32_t i = SLOT_FIRST; i < SLOT_LAST; ++i)
		{
			if(Item* invItem = inventory[i])
			{
				if(invItem == item || invItem == tradeItem)
					continue;

				if(autoStack && item->isStackable() && __queryAdd(i, item, item->getItemCount(), 0)
					== RET_NOERROR && invItem->getID() == item->getID() && invItem->getItemCount() < 100)
				{
					*destItem = invItem;
					index = i;
					return this;
				}

				if(Container* container = invItem->getContainer())
				{
					if(!autoStack && container->__queryAdd(INDEX_WHEREEVER,
						item, item->getItemCount(), flags) == RET_NOERROR)
					{
						index = INDEX_WHEREEVER;
						return container;
					}

					containers.push_back(std::make_pair(container, 0));
				}
			}
			else if(!autoStack)
			{
				if(__queryAdd(i, item, item->getItemCount(), 0) == RET_NOERROR)
				{
					index = i;
					return this;
				}
			}
			else
				freeSlots.push_back(std::make_pair(this, i));
		}

		//int32_t deepness = g_config.getNumber(ConfigManager::PLAYER_DEEPNESS);
		while(!containers.empty())
		{
			Container* tmpContainer = containers.front().first;
			int32_t level = containers.front().second;

			containers.pop_front();
			if(!tmpContainer)
				continue;

			for(uint32_t n = 0; n < tmpContainer->capacity(); ++n)
			{
				if(Item* tmpItem = tmpContainer->getItem(n))
				{
					if(tmpItem == item || tmpItem == tradeItem)
						continue;

					if(autoStack && item->isStackable() && tmpContainer->__queryAdd(n, item, item->getItemCount(),
						0) == RET_NOERROR && tmpItem->getID() == item->getID() && tmpItem->getItemCount() < 100)
					{
						index = n;
						*destItem = tmpItem;
						return tmpContainer;
					}

					if(Container* container = tmpItem->getContainer())
					{
						if(!autoStack && container->__queryAdd(INDEX_WHEREEVER,
							item, item->getItemCount(), flags) == RET_NOERROR)
						{
							index = INDEX_WHEREEVER;
							return container;
						}

						//if(deepness < 0 || level < deepness)
							containers.push_back(std::make_pair(container, level + 1));
					}
				}
				else
				{
					if(!autoStack)
					{
						if(tmpContainer->__queryAdd(n, item, item->getItemCount(), 0) == RET_NOERROR)
						{
							index = n;
							return tmpContainer;
						}
					}
					else
						freeSlots.push_back(std::make_pair(tmpContainer, n));

					break; // one slot to check is definitely enough.
				}
			}
		}

		if(autoStack)
		{
			while(!freeSlots.empty())
			{
				Cylinder* tmpCylinder = freeSlots.front().first;
				int32_t i = freeSlots.front().second;

				freeSlots.pop_front();
				if(!tmpCylinder)
					continue;

				if(tmpCylinder->__queryAdd(i, item, item->getItemCount(), flags) == RET_NOERROR)
				{
					index = i;
					return tmpCylinder;
				}
			}
		}

		return this;
	}

	Thing* destThing = __getThing(index);
	if(destThing)
		*destItem = destThing->getItem();

	if(Cylinder* subCylinder = dynamic_cast<Cylinder*>(destThing))
	{
		index = INDEX_WHEREEVER;
		*destItem = NULL;
		return subCylinder;
	}

	return this;
}

void Player::__addThing(Creature* actor, Thing* thing)
{
	__addThing(actor, 0, thing);
}

void Player::__addThing(Creature*, int32_t index, Thing* thing)
{
	if(index < 0 || index > 11)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__addThing], " << "player: " << getName() << ", index: " << index << ", index < 0 || index > 11" << std::endl;
#endif
		return /*RET_NOTPOSSIBLE*/;
	}

	if(!index)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__addThing], " << "player: " << getName() << ", index == 0" << std::endl;
#endif
		return /*RET_NOTENOUGHROOM*/;
	}

	Item* item = thing->getItem();
	if(!item)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__addThing], " << "player: " << getName() << ", item == NULL" << std::endl;
#endif
		return /*RET_NOTPOSSIBLE*/;
	}

	item->setParent(this);
	inventory[index] = item;

	//send to client
	sendAddInventoryItem((slots_t)index, item);
	//event methods
	onAddInventoryItem((slots_t)index, item);
}

void Player::__updateThing(Thing* thing, uint16_t itemId, uint32_t count)
{
	int32_t index = __getIndexOfThing(thing);
	if(index == -1)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__updateThing], " << "player: " << getName() << ", index == -1" << std::endl;
#endif
		return /*RET_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if(!item)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__updateThing], " << "player: " << getName() << ", item == NULL" << std::endl;
#endif
		return /*RET_NOTPOSSIBLE*/;
	}

	const ItemType& oldType = Item::items[item->getID()];
	const ItemType& newType = Item::items[itemId];

	item->setID(itemId);
	item->setSubType(count);

	//send to client
	sendUpdateInventoryItem((slots_t)index, item, item);
	//event methods
	onUpdateInventoryItem((slots_t)index, item, oldType, item, newType);
}

void Player::__replaceThing(uint32_t index, Thing* thing)
{
	if(index > 11)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__replaceThing], " << "player: " << getName() << ", index: " << index << ", index < 0 || index > 11" << std::endl;
#endif
		return /*RET_NOTPOSSIBLE*/;
	}

	Item* oldItem = getInventoryItem((slots_t)index);
	if(!oldItem)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__updateThing], " << "player: " << getName() << ", oldItem == NULL" << std::endl;
#endif
		return /*RET_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if(!item)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__updateThing], " << "player: " << getName() << ", item == NULL" << std::endl;
#endif
		return /*RET_NOTPOSSIBLE*/;
	}

	const ItemType& oldType = Item::items[oldItem->getID()];
	const ItemType& newType = Item::items[item->getID()];

	//send to client
	sendUpdateInventoryItem((slots_t)index, oldItem, item);
	//event methods
	onUpdateInventoryItem((slots_t)index, oldItem, oldType, item, newType);

	item->setParent(this);
	inventory[index] = item;
}

void Player::__removeThing(Thing* thing, uint32_t count)
{
	Item* item = thing->getItem();
	if(!item)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__removeThing], " << "player: " << getName() << ", item == NULL" << std::endl;
#endif
		return /*RET_NOTPOSSIBLE*/;
	}

	int32_t index = __getIndexOfThing(thing);
	if(index == -1)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__removeThing], " << "player: " << getName() << ", index == -1" << std::endl;
#endif
		return /*RET_NOTPOSSIBLE*/;
	}

	if(item->isStackable())
	{
		if(count == item->getItemCount())
		{
			//send change to client
			sendRemoveInventoryItem((slots_t)index, item);
			//event methods
			onRemoveInventoryItem((slots_t)index, item);

			item->setParent(NULL);
			inventory[index] = NULL;
		}
		else
		{
			item->setItemCount(std::max(0, (int32_t)(item->getItemCount() - count)));
			const ItemType& it = Item::items[item->getID()];

			//send change to client
			sendUpdateInventoryItem((slots_t)index, item, item);
			//event methods
			onUpdateInventoryItem((slots_t)index, item, it, item, it);
		}
	}
	else
	{
		//send change to client
		sendRemoveInventoryItem((slots_t)index, item);
		//event methods
		onRemoveInventoryItem((slots_t)index, item);

		item->setParent(NULL);
		inventory[index] = NULL;
	}
}

Thing* Player::__getThing(uint32_t index) const
{
	if(index > SLOT_PRE_FIRST && index < SLOT_LAST)
		return inventory[index];

	return NULL;
}

int32_t Player::__getIndexOfThing(const Thing* thing) const
{
	for(int32_t i = SLOT_FIRST; i < SLOT_LAST; ++i)
	{
		if(inventory[i] == thing)
			return i;
	}

	return -1;
}

int32_t Player::__getFirstIndex() const
{
	return SLOT_FIRST;
}

int32_t Player::__getLastIndex() const
{
	return SLOT_LAST;
}

uint32_t Player::__getItemTypeCount(uint16_t itemId, int32_t subType /*= -1*/) const
{
	Item* item = NULL;
	Container* container = NULL;

	uint32_t count = 0;
	for(int32_t i = SLOT_FIRST; i < SLOT_LAST; ++i)
	{
		if(!(item = inventory[i]))
			continue;

		if(item->getID() != itemId)
		{
			if(!(container = item->getContainer()))
				continue;

			for(ContainerIterator it = container->begin(), end = container->end(); it != end; ++it)
			{
				if((*it)->getID() == itemId)
					count += Item::countByType(*it, subType);
			}
		}
		else
			count += Item::countByType(item, subType);
	}

	return count;

}

std::map<uint32_t, uint32_t>& Player::__getAllItemTypeCount(std::map<uint32_t, uint32_t>& countMap) const
{
	Item* item = NULL;
	Container* container = NULL;
	for(int32_t i = SLOT_FIRST; i < SLOT_LAST; ++i)
	{
		if(!(item = inventory[i]))
			continue;

		countMap[item->getID()] += Item::countByType(item, -1);
		if(!(container = item->getContainer()))
			continue;

		for(ContainerIterator it = container->begin(), end = container->end(); it != end; ++it)
			countMap[(*it)->getID()] += Item::countByType(*it, -1);
	}

	return countMap;
}

void Player::postAddNotification(Creature*, Thing* thing, const Cylinder* oldParent,
	int32_t index, cylinderlink_t link /*= LINK_OWNER*/)
{
	if(link == LINK_OWNER) //calling movement scripts
		g_moveEvents->onPlayerEquip(this, thing->getItem(), (slots_t)index, false);

	bool requireListUpdate = true;
	if(link == LINK_OWNER || link == LINK_TOPPARENT)
	{
		if(const Item* item = (oldParent ? oldParent->getItem() : NULL))
		{
			assert(item->getContainer() != NULL);
			requireListUpdate = item->getContainer()->getHoldingPlayer() != this;
		}
		else
			requireListUpdate = oldParent != this;

		updateInventoryWeight();
		updateItemsLight();
		updateWeapon();
		sendStats();
	}

	if(const Item* item = thing->getItem())
	{
		if(const Container* container = item->getContainer())
			onSendContainer(container);

		if(shopOwner && requireListUpdate)
			updateInventoryGoods(item->getID());
	}
	else if(const Creature* creature = thing->getCreature())
	{
		if(creature != this)
			return;

		std::vector<Container*> containers;
		for(ContainerVector::iterator it = containerVec.begin(); it != containerVec.end(); ++it)
		{
			if(!Position::areInRange<1,1,0>(it->second->getPosition(), getPosition()))
				containers.push_back(it->second);
		}

		for(std::vector<Container*>::const_iterator it = containers.begin(); it != containers.end(); ++it)
			autoCloseContainers(*it);
	}
}

void Player::postRemoveNotification(Creature*, Thing* thing, const Cylinder* newParent,
	int32_t index, bool isCompleteRemoval, cylinderlink_t link /*= LINK_OWNER*/)
{
	if(link == LINK_OWNER) //calling movement scripts
		g_moveEvents->onPlayerDeEquip(this, thing->getItem(), (slots_t)index, isCompleteRemoval);

	bool requireListUpdate = true;
	if(link == LINK_OWNER || link == LINK_TOPPARENT)
	{
		if(const Item* item = (newParent ? newParent->getItem() : NULL))
		{
			assert(item->getContainer() != NULL);
			requireListUpdate = item->getContainer()->getHoldingPlayer() != this;
		}
		else
			requireListUpdate = newParent != this;

		updateInventoryWeight();
		updateItemsLight();
		updateWeapon();
		sendStats();
	}

	if(const Item* item = thing->getItem())
	{
		if(const Container* container = item->getContainer())
		{
			if(container->isRemoved() || !Position::areInRange<1,1,0>(getPosition(), container->getPosition()))
				autoCloseContainers(container);
			else if(container->getTopParent() == this)
				onSendContainer(container);
			else if(const Container* topContainer = dynamic_cast<const Container*>(container->getTopParent()))
			{
				if(const Depot* depot = dynamic_cast<const Depot*>(topContainer))
				{
					bool isOwner = false;
					for(DepotMap::iterator it = depots.begin(); it != depots.end(); ++it)
					{
						if(it->second.first != depot)
							continue;

						isOwner = true;
						onSendContainer(container);
					}

					if(!isOwner)
						autoCloseContainers(container);
				}
				else
					onSendContainer(container);
			}
			else
				autoCloseContainers(container);
		}

		if(shopOwner && requireListUpdate)
			updateInventoryGoods(item->getID());
	}
}

void Player::__internalAddThing(Thing* thing)
{
	__internalAddThing(0, thing);
}

void Player::__internalAddThing(uint32_t index, Thing* thing)
{
#ifdef __DEBUG_MOVESYS__
	std::clog << "[Player::__internalAddThing] index: " << index << std::endl;

#endif
	if(!index || index > 11)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__internalAddThing] index == 0 || index > 11" << std::endl;
#endif
		return;
	}

	if(inventory[index])
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Warning: [Player::__internalAddThing], player: " << getName() << ", items[index] is not empty." << std::endl;
#endif
		return;
	}

	Item* item = thing->getItem();
	if(!item)
	{
#ifdef __DEBUG_MOVESYS__
		std::clog << "Failure: [Player::__internalAddThing] item == NULL" << std::endl;
#endif
		return;
	}

	inventory[index] = item;
	item->setParent(this);
}

bool Player::setFollowCreature(Creature* creature, bool fullPathSearch /*= false*/)
{
	bool deny = false;
	CreatureEventList followEvents = getCreatureEvents(CREATURE_EVENT_FOLLOW);
	for(CreatureEventList::iterator it = followEvents.begin(); it != followEvents.end(); ++it)
	{
		if(creature && !(*it)->executeFollow(this, creature))
			deny = true;
	}

	if(!deny && Creature::setFollowCreature(creature, fullPathSearch))
		return true;

	setFollowCreature(NULL);
	setAttackedCreature(NULL);
	if(!deny)
		sendCancelMessage(RET_THEREISNOWAY);

	sendCancelTarget();
	cancelNextWalk = true;
	return false;
}

bool Player::setAttackedCreature(Creature* creature)
{
	if(!Creature::setAttackedCreature(creature))
	{
		sendCancelTarget();
		return false;
	}

	if(chaseMode == CHASEMODE_FOLLOW && creature)
	{
		if(followCreature != creature) //chase opponent
			setFollowCreature(creature);
	}
	else
		setFollowCreature(NULL);

	if(creature)
		Dispatcher::getInstance().addTask(createTask(boost::bind(&Game::checkCreatureAttack, &g_game, getID())));

	return true;
}

void Player::goToFollowCreature()
{
	if(!walkTask)
		Creature::goToFollowCreature();
}

void Player::getPathSearchParams(const Creature* creature, FindPathParams& fpp) const
{
	Creature::getPathSearchParams(creature, fpp);
	fpp.fullPathSearch = true;
}

void Player::doAttacking(uint32_t)
{
	if(!lastAttack)
		lastAttack = OTSYS_TIME() - getAttackSpeed() - 1;
	else if((OTSYS_TIME() - lastAttack) < getAttackSpeed())
		return;

	if(hasCondition(CONDITION_PACIFIED) && !hasCustomFlag(PlayerCustomFlag_IgnorePacification))
	{
		lastAttack = OTSYS_TIME();
		return;
	}

	Item* item = getWeapon(false);
	if(const Weapon* _weapon = g_weapons->getWeapon(item))
	{
		if(_weapon->interruptSwing() && !canDoAction())
		{
			SchedulerTask* task = createSchedulerTask(getNextActionTime(),
				boost::bind(&Game::checkCreatureAttack, &g_game, getID()));
			setNextActionTask(task);
		}
		else
		{
			//if((!_weapon->hasExhaustion() || !hasCondition(CONDITION_EXHAUST, EXHAUST_COMBAT)) && _weapon->useWeapon(this, item, attackedCreature))
			if(!_weapon->hasExhaustion() && _weapon->useWeapon(this, item, attackedCreature))
				lastAttack = OTSYS_TIME();

			updateWeapon();
		}
	}
	else if(Weapon::useFist(this, attackedCreature))
		lastAttack = OTSYS_TIME();
}

double Player::getGainedExperience(Creature* attacker) const
{
	if(!skillLoss)
		return 0;

	double rate = g_config.getDouble(ConfigManager::RATE_PVP_EXPERIENCE);
	if(rate <= 0)
		return 0;

	Player* attackerPlayer = attacker->getPlayer();
	if(!attackerPlayer || attackerPlayer == this)
		return 0;

	double attackerLevel = (double)attackerPlayer->getLevel(), min = g_config.getDouble(
		ConfigManager::EFP_MIN_THRESHOLD), max = g_config.getDouble(ConfigManager::EFP_MAX_THRESHOLD);
	if((min > 0 && level < (uint32_t)std::floor(attackerLevel * min)) || (max > 0 &&
		level > (uint32_t)std::floor(attackerLevel * max)))
		return 0;

	/*
		Formula
		a = attackers level * 0.9
		b = victims level
		c = victims experience

		result = (1 - (a / b)) * 0.05 * c
		Not affected by special multipliers(!)
	*/
	uint32_t a = (uint32_t)std::floor(attackerLevel * 0.9), b = level;
	uint64_t c = getExperience();
	return (double)std::max((uint64_t)0, (uint64_t)std::floor(getDamageRatio(attacker)
		* std::max((double)0, ((double)(1 - (((double)a / b))))) * 0.05 * c)) * rate;
}

void Player::onFollowCreature(const Creature* creature)
{
	if(!creature)
		cancelNextWalk = true;
}

void Player::setChaseMode(chaseMode_t mode)
{
	chaseMode_t prevChaseMode = chaseMode;
	chaseMode = mode;

	if(prevChaseMode == chaseMode)
		return;

	if(chaseMode == CHASEMODE_FOLLOW)
	{
		if(!followCreature && attackedCreature) //chase opponent
			setFollowCreature(attackedCreature);
	}
	else if(attackedCreature)
	{
		setFollowCreature(NULL);
		cancelNextWalk = true;
	}
}

void Player::onWalkAborted()
{
	setNextWalkActionTask(NULL);
	sendCancelWalk();
}

void Player::onWalkComplete()
{
	if(!walkTask)
		return;

	walkTaskEvent = Scheduler::getInstance().addEvent(walkTask);
	walkTask = NULL;
}

void Player::getCreatureLight(LightInfo& light) const
{
	if(internalLight.level > itemsLight.level)
		light = internalLight;
	else
		light = itemsLight;
}

void Player::updateItemsLight(bool internal/* = false*/)
{
	LightInfo maxLight, curLight;
	Item* item = NULL;
	for(int32_t i = SLOT_FIRST; i < SLOT_LAST; ++i)
	{
		if(!(item = getInventoryItem((slots_t)i)))
			continue;

		item->getLight(curLight);
		if(curLight.level > maxLight.level)
			maxLight = curLight;
	}

	if(maxLight.level > itemsLight.level || (maxLight.level == itemsLight.level && maxLight.color != itemsLight.color))
	{
		itemsLight = maxLight;
		if(!internal)
			g_game.changeLight(this);
	}
}

void Player::onAddCondition(ConditionType_t type, bool hadCondition)
{
	Creature::onAddCondition(type, hadCondition);
	if(getLastPosition().x && type != CONDITION_GAMEMASTER) // don't send if player have just logged in (its already done in protocolgame), or condition have no icons
		sendIcons();
}

void Player::onAddCombatCondition(ConditionType_t type, bool)
{
	std::string tmp;
	switch(type)
	{
		//client hardcoded
		case CONDITION_FIRE:
			tmp = "burning";
			break;
		case CONDITION_POISON:
			tmp = "poisoned";
			break;
		case CONDITION_ENERGY:
			tmp = "electrified";
			break;
		case CONDITION_FREEZING:
			tmp = "freezing";
			break;
		case CONDITION_DAZZLED:
			tmp = "dazzled";
			break;
		case CONDITION_CURSED:
			tmp = "cursed";
			break;
		case CONDITION_DROWN:
			tmp = "drowning";
			break;
		case CONDITION_DRUNK:
			tmp = "drunk";
			break;
		case CONDITION_PARALYZE:
			tmp = "paralyzed";
			break;
		/*case CONDITION_MANASHIELD:
			tmp = "protected by a magic shield";
			break;
		case CONDITION_HASTE:
			tmp = "hasted";
			break;
		case CONDITION_ATTRIBUTES:
			tmp = "strengthened";
			break;*/
		default:
			break;
	}

	if(!tmp.empty())
		sendTextMessage(MSG_STATUS_DEFAULT, "You are " + tmp + ".");
}

void Player::onEndCondition(ConditionType_t type)
{
	Creature::onEndCondition(type);
	if(type == CONDITION_INFIGHT)
	{
		onIdleStatus();
		clearAttacked();

		pzLocked = false;
		if(skull < SKULL_RED)
			setSkull(SKULL_NONE);

		g_game.updateCreatureSkull(this);
	}

	sendIcons();
}

void Player::onCombatRemoveCondition(const Creature*, Condition* condition)
{
	//Creature::onCombatRemoveCondition(attacker, condition);
	bool remove = true;
	if(condition->getId() > 0)
	{
		remove = false;
		//Means the condition is from an item, id == slot
		if(g_game.getWorldType() == WORLDTYPE_HARDCORE)
		{
			if(Item* item = getInventoryItem((slots_t)condition->getId()))
			{
				//25% chance to destroy the item
				if(random_range(1, 100) < 26)
					g_game.internalRemoveItem(NULL, item);
			}
		}
	}

	if(remove)
	{
		if(!canDoAction())
		{
			int32_t delay = getNextActionTime(false);
			delay -= (delay % EVENT_CREATURE_THINK_INTERVAL);
			if(delay < 0)
				removeCondition(condition);
			else
				condition->setTicks(delay);
		}
		else
			removeCondition(condition);
	}
}

void Player::onTickCondition(ConditionType_t type, int32_t interval, bool& _remove)
{
	Creature::onTickCondition(type, interval, _remove);
	if(type == CONDITION_HUNTING)
		useStamina(-(interval * g_config.getNumber(ConfigManager::RATE_STAMINA_LOSS)));
}

void Player::onAttackedCreature(Creature* target)
{
	Creature::onAttackedCreature(target);
	if(hasFlag(PlayerFlag_NotGainInFight))
		return;

	addInFightTicks(false);
	Player* targetPlayer = target->getPlayer();
	if(!targetPlayer)
		return;

	addAttacked(targetPlayer);
	if(targetPlayer == this && targetPlayer->getZone() != ZONE_HARDCORE)
	{
		targetPlayer->sendCreatureSkull(this);
		return;
	}

	if(Combat::isInPvpZone(this, targetPlayer) || isPartner(targetPlayer) ||
#ifdef __WAR_SYSTEM__
		isAlly(targetPlayer) ||
#endif
		(g_config.getBool(ConfigManager::ALLOW_FIGHTBACK) && targetPlayer->hasAttacked(this)
#ifdef __WAR_SYSTEM__
		&& !targetPlayer->isEnemy(this, false)
#endif
		))
		return;

	if(!pzLocked)
	{
		pzLocked = true;
		sendIcons();
	}

	if(getZone() != target->getZone() || skull != SKULL_NONE
#ifdef __WAR_SYSTEM__
		|| targetPlayer->isEnemy(this, true)
#endif
		)
		return;

	if(targetPlayer->getSkull() != SKULL_NONE)
		targetPlayer->sendCreatureSkull(this);
	else if(!hasCustomFlag(PlayerCustomFlag_NotGainSkull))
	{
		setSkull(SKULL_WHITE);
		g_game.updateCreatureSkull(this);
	}
}

void Player::onSummonAttackedCreature(Creature* summon, Creature* target)
{
	Creature::onSummonAttackedCreature(summon, target);
	onAttackedCreature(target);
}

void Player::onAttacked()
{
	Creature::onAttacked();
	addInFightTicks(false);
}

bool Player::checkLoginDelay(uint32_t playerId) const
{
	return (!hasCustomFlag(PlayerCustomFlag_IgnoreLoginDelay) && OTSYS_TIME() <= (lastLoad + g_config.getNumber(
		ConfigManager::LOGIN_PROTECTION)) && !hasBeenAttacked(playerId));
}

void Player::onIdleStatus()
{
	Creature::onIdleStatus();
	if(getParty())
		getParty()->clearPlayerPoints(this);
}

void Player::onPlacedCreature()
{
	//scripting event - onLogin
	if(!g_creatureEvents->playerLogin(this))
		kick(true, true);
}

void Player::onAttackedCreatureDrain(Creature* target, int32_t points)
{
	Creature::onAttackedCreatureDrain(target, points);
	if(party && target && (!target->getMaster() || !target->getMaster()->getPlayer())
		&& target->getMonster() && target->getMonster()->isHostile()) //we have fulfilled a requirement for shared experience
		getParty()->addPlayerDamageMonster(this, points);

	char buffer[100];
	sprintf(buffer, "You deal %d damage to %s.", points, target->getNameDescription().c_str());
	sendTextMessage(MSG_STATUS_DEFAULT, buffer);
}

void Player::onSummonAttackedCreatureDrain(Creature* summon, Creature* target, int32_t points)
{
	Creature::onSummonAttackedCreatureDrain(summon, target, points);

	char buffer[100];
	sprintf(buffer, "Your %s deals %d damage to %s.", summon->getName().c_str(), points, target->getNameDescription().c_str());
	sendTextMessage(MSG_EVENT_DEFAULT, buffer);
}

void Player::onTargetCreatureGainHealth(Creature* target, int32_t points)
{
	Creature::onTargetCreatureGainHealth(target, points);
	if(target && getParty())
	{
		Player* tmpPlayer = NULL;
		if(target->getPlayer())
			tmpPlayer = target->getPlayer();
		else if(target->getMaster() && target->getMaster()->getPlayer())
			tmpPlayer = target->getMaster()->getPlayer();

		if(isPartner(tmpPlayer))
			party->addPlayerHealedMember(this, points);
	}
}
#ifdef __WAR_SYSTEM__

GuildEmblems_t Player::getGuildEmblem(const Creature* creature) const
{
	const Player* player = creature->getPlayer();
	if(!player || !player->hasEnemy())
		return Creature::getGuildEmblem(creature);

	if(player->isEnemy(this, false))
		return EMBLEM_RED;

	return player->getGuildId() == guildId ? EMBLEM_GREEN : EMBLEM_BLUE;
}

bool Player::getEnemy(const Player* player, War_t& data) const
{
	if(!guildId || !player || player->isRemoved())
		return false;

	uint32_t guild = player->getGuildId();
	if(!guild)
		return false;

	WarMap::const_iterator it = warMap.find(guild);
	if(it == warMap.end())
		return false;

	data = it->second;
	return true;
}

bool Player::isEnemy(const Player* player, bool allies) const
{
	if(!guildId || !player || player->isRemoved())
		return false;

	uint32_t guild = player->getGuildId();
	if(!guild)
		return false;

	return !warMap.empty() && (((g_game.getWorldType() != WORLDTYPE_OPTIONAL || g_config.getBool(
		ConfigManager::OPTIONAL_WAR_ATTACK_ALLY)) && allies && guildId == guild) ||
		warMap.find(guild) != warMap.end());
}

bool Player::isAlly(const Player* player) const
{
	return !warMap.empty() && player && player->getGuildId() == guildId;
}
#endif

bool Player::onKilledCreature(Creature* target, DeathEntry& entry)
{
	if(!Creature::onKilledCreature(target, entry))
		return false;

	if(hasFlag(PlayerFlag_NotGenerateLoot))
		target->setDropLoot(LOOT_DROP_NONE);

	Condition* condition = NULL;
	if(target->getMonster() && !target->isPlayerSummon() && !hasFlag(PlayerFlag_HasInfiniteStamina)
		&& (condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_HUNTING,
		g_config.getNumber(ConfigManager::HUNTING_DURATION))))
		addCondition(condition);

	if(hasFlag(PlayerFlag_NotGainInFight) || getZone() != target->getZone())
		return true;

	Player* targetPlayer = target->getPlayer();
	if(!targetPlayer || Combat::isInPvpZone(this, targetPlayer) || isPartner(targetPlayer)
#ifdef __WAR_SYSTEM__
		|| isAlly(targetPlayer)
#endif
		)
		return true;
#ifdef __WAR_SYSTEM__

	War_t enemy;
	if(targetPlayer->getEnemy(this, enemy) && (!entry.isLast() || IOGuild::getInstance()->updateWar(enemy)))
		entry.setWar(enemy);

#endif

	if(!entry.isJustify() || !hasCondition(CONDITION_INFIGHT))
		return true;

	if(!targetPlayer->hasAttacked(this) && target->getSkull() == SKULL_NONE && targetPlayer != this
		&& (addUnjustifiedKill(targetPlayer,
#ifndef __WAR_SYSTEM__
		true
#else
		!enemy.war
#endif
		) || entry.isLast()))
		entry.setUnjustified();

	addInFightTicks(true, g_config.getNumber(ConfigManager::WHITE_SKULL_TIME));
	return true;
}

bool Player::gainExperience(double& gainExp, bool fromMonster)
{
	if(!rateExperience(gainExp, fromMonster))
		return false;

	//soul regeneration
	if(gainExp >= level)
	{
		if(Condition* condition = Condition::createCondition(
			CONDITIONID_DEFAULT, CONDITION_SOUL, 4 * 60 * 1000))
		{
			condition->setParam(CONDITIONPARAM_SOULGAIN,
				vocation->getGainAmount(GAIN_SOUL));
			condition->setParam(CONDITIONPARAM_SOULTICKS,
				(vocation->getGainTicks(GAIN_SOUL) * 1000));
			addCondition(condition);
		}
	}

	addExperience((uint64_t)gainExp);
	return true;
}

bool Player::rateExperience(double& gainExp, bool fromMonster)
{
	if(hasFlag(PlayerFlag_NotGainExperience) || gainExp <= 0)
		return false;

	if(!fromMonster)
		return true;

	gainExp *= rates[SKILL__LEVEL] * g_game.getExperienceStage(level,
		vocation->getExperienceMultiplier());
	if(!hasFlag(PlayerFlag_HasInfiniteStamina))
	{
		int32_t minutes = getStaminaMinutes();
		if(minutes >= g_config.getNumber(ConfigManager::STAMINA_LIMIT_TOP))
		{
			if(isPremium() || !g_config.getNumber(ConfigManager::STAMINA_BONUS_PREMIUM))
				gainExp *= g_config.getDouble(ConfigManager::RATE_STAMINA_ABOVE);
		}
		else if(minutes < (g_config.getNumber(ConfigManager::STAMINA_LIMIT_BOTTOM)) && minutes > 0)
			gainExp *= g_config.getDouble(ConfigManager::RATE_STAMINA_UNDER);
		else if(minutes <= 0)
			gainExp = 0;
	}
	else if(isPremium() || !g_config.getNumber(ConfigManager::STAMINA_BONUS_PREMIUM))
		gainExp *= g_config.getDouble(ConfigManager::RATE_STAMINA_ABOVE);

	return true;
}

void Player::onGainExperience(double& gainExp, bool fromMonster, bool multiplied)
{
	if(party && party->isSharedExperienceEnabled() && party->isSharedExperienceActive())
	{
		party->shareExperience(gainExp, fromMonster, multiplied);
		rateExperience(gainExp, fromMonster);
		return; //we will get a share of the experience through the sharing mechanism
	}

	if(gainExperience(gainExp, fromMonster))
		Creature::onGainExperience(gainExp, fromMonster, true);
}

void Player::onGainSharedExperience(double& gainExp, bool fromMonster, bool)
{
	if(gainExperience(gainExp, fromMonster))
		Creature::onGainSharedExperience(gainExp, fromMonster, true);
}

bool Player::isImmune(CombatType_t type) const
{
	return hasCustomFlag(PlayerCustomFlag_IsImmune) || Creature::isImmune(type);
}

bool Player::isImmune(ConditionType_t type) const
{
	return hasCustomFlag(PlayerCustomFlag_IsImmune) || Creature::isImmune(type);
}

bool Player::isAttackable() const
{
	return (!hasFlag(PlayerFlag_CannotBeAttacked) && !isAccountManager());
}

void Player::changeHealth(int32_t healthChange)
{
	Creature::changeHealth(healthChange);
	sendStats();
}

void Player::changeMana(int32_t manaChange)
{
	if(!hasFlag(PlayerFlag_HasInfiniteMana))
		Creature::changeMana(manaChange);

	sendStats();
}

void Player::changeSoul(int32_t soulChange)
{
	if(!hasFlag(PlayerFlag_HasInfiniteSoul))
		soul = std::min((int32_t)soulMax, (int32_t)soul + soulChange);

	sendStats();
}

bool Player::changeOutfit(Outfit_t outfit, bool checkList)
{
	uint32_t outfitId = Outfits::getInstance()->getOutfitId(outfit.lookType);
	if(checkList && (!canWearOutfit(outfitId, outfit.lookAddons) || !requestedOutfit))
		return false;

	requestedOutfit = false;
	if(outfitAttributes)
	{
		uint32_t oldId = Outfits::getInstance()->getOutfitId(defaultOutfit.lookType);
		outfitAttributes = !Outfits::getInstance()->removeAttributes(getID(), oldId, sex);
	}

	defaultOutfit = outfit;
	outfitAttributes = Outfits::getInstance()->addAttributes(getID(), outfitId, sex, defaultOutfit.lookAddons);
	return true;
}

bool Player::canWearOutfit(uint32_t outfitId, uint32_t addons)
{
	OutfitMap::iterator it = outfits.find(outfitId);
	if(it == outfits.end() || (it->second.isPremium && !isPremium()) || getAccess() < it->second.accessLevel
		|| ((it->second.addons & addons) != addons && !hasCustomFlag(PlayerCustomFlag_CanWearAllAddons)))
		return false;

	if(it->second.storageId.empty())
		return true;

	std::string value;
	getStorage(it->second.storageId, value);

	bool ret = value == it->second.storageValue;
	if(ret)
		return ret;

	int32_t tmp = atoi(value.c_str());
	if(!tmp && value != "0")
		return ret;

	tmp = atoi(it->second.storageValue.c_str());
	if(!tmp && it->second.storageValue != "0")
		return ret;

	return atoi(value.c_str()) >= tmp;
}

bool Player::addOutfit(uint32_t outfitId, uint32_t addons)
{
	Outfit outfit;
	if(!Outfits::getInstance()->getOutfit(outfitId, sex, outfit))
		return false;

	OutfitMap::iterator it = outfits.find(outfitId);
	if(it != outfits.end())
		outfit.addons |= it->second.addons;

	outfit.addons |= addons;
	outfits[outfitId] = outfit;
	return true;
}

bool Player::removeOutfit(uint32_t outfitId, uint32_t addons)
{
	OutfitMap::iterator it = outfits.find(outfitId);
	if(it == outfits.end())
		return false;

	if(addons == 0xFF) //remove outfit
		outfits.erase(it);
	else //remove addons
		outfits[outfitId].addons = it->second.addons & (~addons);

	return true;
}

void Player::generateReservedStorage()
{
	uint32_t key = PSTRG_OUTFITSID_RANGE_START + 1;
	const OutfitMap& defaultOutfits = Outfits::getInstance()->getOutfits(sex);
	for(OutfitMap::const_iterator it = outfits.begin(); it != outfits.end(); ++it)
	{
		OutfitMap::const_iterator dit = defaultOutfits.find(it->first);
		if(dit == defaultOutfits.end() || (dit->second.isDefault && (dit->second.addons
			& it->second.addons) == it->second.addons))
			continue;

		std::stringstream k, v;
		k << key++; // this may not work as intended, revalidate it
		v << ((it->first << 16) | (it->second.addons & 0xFF));

		storageMap[k.str()] = v.str();
		if(key <= PSTRG_OUTFITSID_RANGE_START + PSTRG_OUTFITSID_RANGE_SIZE)
			continue;

		std::clog << "[Warning - Player::genReservedStorageRange] Player " << getName() << " with more than 500 outfits!" << std::endl;
		break;
	}
}

void Player::setSex(uint16_t newSex)
{
	sex = newSex;
	const OutfitMap& defaultOutfits = Outfits::getInstance()->getOutfits(sex);
	for(OutfitMap::const_iterator it = defaultOutfits.begin(); it != defaultOutfits.end(); ++it)
	{
		if(it->second.isDefault)
			addOutfit(it->first, it->second.addons);
	}
}

Skulls_t Player::getSkull() const
{
	if(hasFlag(PlayerFlag_NotGainInFight) || hasCustomFlag(PlayerCustomFlag_NotGainSkull))
		return SKULL_NONE;

	return skull;
}

Skulls_t Player::getSkullType(const Creature* creature) const
{
	if(const Player* player = creature->getPlayer())
	{
		if(g_game.getWorldType() != WORLDTYPE_OPEN)
			return SKULL_NONE;

		if((player == this || (skull != SKULL_NONE && player->getSkull() < SKULL_RED)) && player->hasAttacked(this)
#ifdef __WAR_SYSTEM__
			&& !player->isEnemy(this, false)
#endif
			)
			return SKULL_YELLOW;

		if(player->getSkull() == SKULL_NONE &&
#ifndef __WAR_SYSTEM__
			isPartner(player) &&
#else
			(isPartner(player) || isAlly(player)) &&
#endif
			g_game.getWorldType() != WORLDTYPE_OPTIONAL)
			return SKULL_GREEN;
	}

	return Creature::getSkullType(creature);
}

bool Player::hasAttacked(const Player* attacked) const
{
	return !hasFlag(PlayerFlag_NotGainInFight) && attacked &&
		attackedSet.find(attacked->getID()) != attackedSet.end();
}

void Player::addAttacked(const Player* attacked)
{
	if(hasFlag(PlayerFlag_NotGainInFight) || !attacked)
		return;

	uint32_t attackedId = attacked->getID();
	if(attackedSet.find(attackedId) == attackedSet.end())
		attackedSet.insert(attackedId);
}

void Player::setSkullEnd(time_t _time, bool login, Skulls_t _skull)
{
	if(g_game.getWorldType() != WORLDTYPE_OPEN
		|| hasFlag(PlayerFlag_NotGainInFight) ||
		hasCustomFlag(PlayerCustomFlag_NotGainSkull))
		return;

	bool requireUpdate = false;
	if(_time > time(NULL))
	{
		requireUpdate = true;
		setSkull(_skull);
	}
	else if(skull == _skull)
	{
		requireUpdate = true;
		setSkull(SKULL_NONE);
		_time = 0;
	}

	if(requireUpdate)
	{
		skullEnd = _time;
		if(!login)
			g_game.updateCreatureSkull(this);
	}
}

bool Player::addUnjustifiedKill(const Player* attacked, bool countNow)
{
	if(!g_config.getBool(ConfigManager::USE_FRAG_HANDLER) || hasFlag(
		PlayerFlag_NotGainInFight) || g_game.getWorldType() != WORLDTYPE_OPEN
		|| hasCustomFlag(PlayerCustomFlag_NotGainUnjustified) || hasCustomFlag(
		PlayerCustomFlag_NotGainSkull) || attacked == this)
		return false;

	if(client && countNow)
	{
		char buffer[90];
		sprintf(buffer, "Warning! The murder of %s was not justified.",
			attacked->getName().c_str());
		client->sendTextMessage(MSG_STATUS_WARNING, buffer);
	}

	time_t now = time(NULL), today = (now - 84600), week = (now - (7 * 84600));
	std::vector<time_t> dateList;

	IOLoginData::getInstance()->getUnjustifiedDates(guid, dateList, now);
	if(countNow)
		dateList.push_back(now);

	uint32_t tc = 0, wc = 0, mc = dateList.size();
	for(std::vector<time_t>::iterator it = dateList.begin(); it != dateList.end(); ++it)
	{
		if((*it) > week)
			wc++;

		if((*it) > today)
			tc++;
	}

	uint32_t d = g_config.getNumber(ConfigManager::RED_DAILY_LIMIT), w = g_config.getNumber(
		ConfigManager::RED_WEEKLY_LIMIT), m = g_config.getNumber(ConfigManager::RED_MONTHLY_LIMIT);
	if(skull < SKULL_RED && ((d > 0 && tc >= d) || (w > 0 && wc >= w) || (m > 0 && mc >= m)))
		setSkullEnd(now + g_config.getNumber(ConfigManager::RED_SKULL_LENGTH), false, SKULL_RED);

	if(!g_config.getBool(ConfigManager::USE_BLACK_SKULL))
	{
		d += g_config.getNumber(ConfigManager::BAN_DAILY_LIMIT);
		w += g_config.getNumber(ConfigManager::BAN_WEEKLY_LIMIT);
		m += g_config.getNumber(ConfigManager::BAN_MONTHLY_LIMIT);
		if((d <= 0 || tc < d) && (w <= 0 || wc < w) && (m <= 0 || mc < m))
			return true;

		if(!IOBan::getInstance()->addAccountBanishment(accountId, (now + g_config.getNumber(
			ConfigManager::KILLS_BAN_LENGTH)), 20, ACTION_BANISHMENT, "Unjustified player killing.", 0, guid))
			return true;

		sendTextMessage(MSG_INFO_DESCR, "You have been banished.");
		g_game.addMagicEffect(getPosition(), MAGIC_EFFECT_WRAPS_GREEN);
		Scheduler::getInstance().addEvent(createSchedulerTask(1000, boost::bind(
			&Game::kickPlayer, &g_game, getID(), false)));
	}
	else
	{
		d += g_config.getNumber(ConfigManager::BLACK_DAILY_LIMIT);
		w += g_config.getNumber(ConfigManager::BLACK_WEEKLY_LIMIT);
		m += g_config.getNumber(ConfigManager::BLACK_MONTHLY_LIMIT);
		if(skull < SKULL_BLACK && ((d > 0 && tc >= d) || (w > 0 && wc >= w) || (m > 0 && mc >= m)))
		{
			setSkullEnd(now + g_config.getNumber(ConfigManager::BLACK_SKULL_LENGTH), false, SKULL_BLACK);
			setAttackedCreature(NULL);
			destroySummons();
		}
	}

	return true;
}

void Player::setPromotionLevel(uint32_t pLevel)
{
	if(pLevel > promotionLevel)
	{
		uint32_t tmpLevel = 0, currentVoc = vocationId;
		for(uint32_t i = promotionLevel; i < pLevel; ++i)
		{
			currentVoc = Vocations::getInstance()->getPromotedVocation(currentVoc);
			if(!currentVoc)
				break;

			tmpLevel++;
			Vocation* voc = Vocations::getInstance()->getVocation(currentVoc);
			if(voc->isPremiumNeeded() && !isPremium() && g_config.getBool(ConfigManager::PREMIUM_FOR_PROMOTION))
				continue;

			vocationId = currentVoc;
		}

		promotionLevel += tmpLevel;
	}
	else if(pLevel < promotionLevel)
	{
		uint32_t tmpLevel = 0, currentVoc = vocationId;
		for(uint32_t i = pLevel; i < promotionLevel; ++i)
		{
			Vocation* voc = Vocations::getInstance()->getVocation(currentVoc);
			if(voc->getFromVocation() == currentVoc)
				break;

			tmpLevel++;
			currentVoc = voc->getFromVocation();
			if(voc->isPremiumNeeded() && !isPremium() && g_config.getBool(ConfigManager::PREMIUM_FOR_PROMOTION))
				continue;

			vocationId = currentVoc;
		}

		promotionLevel -= tmpLevel;
	}

	setVocation(vocationId);
}

uint16_t Player::getBlessings() const
{
	if(!g_config.getBool(ConfigManager::BLESSINGS) || (!isPremium() &&
		g_config.getBool(ConfigManager::BLESSING_ONLY_PREMIUM)))
		return 0;

	uint16_t count = 0;
	for(int16_t i = 0; i < 16; ++i)
	{
		if(hasBlessing(i))
			count++;
	}

	return count;
}

uint64_t Player::getLostExperience() const
{
	if(!skillLoss)
		return 0;

	double percent = (double)(lossPercent[LOSS_EXPERIENCE] - vocation->getLessLoss() - (getBlessings() * g_config.getNumber(
		ConfigManager::BLESS_REDUCTION))) / 100.;
	if(level <= 25)
		return (uint64_t)std::floor((double)(experience * percent) / 10.);

	int32_t base = level;
	double levels = (double)(base + 50) / 100.;

	uint64_t lost = 0;
	while(levels > 1.0f)
	{
		lost += (getExpForLevel(base) - getExpForLevel(base - 1));
		base--;
		levels -= 1.;
	}

	if(levels > 0.)
		lost += (uint64_t)std::floor((double)(getExpForLevel(base) - getExpForLevel(base - 1)) * levels);

	return (uint64_t)std::floor((double)(lost * percent));
}

uint32_t Player::getAttackSpeed() const
{
	return ((weapon && weapon->getAttackSpeed() != 0) ? weapon->getAttackSpeed() : (vocation->getAttackSpeed() / std::max((size_t)1, getWeapons().size())));
}

void Player::learnInstantSpell(const std::string& name)
{
	if(!hasLearnedInstantSpell(name))
		learnedInstantSpellList.push_back(name);
}

void Player::unlearnInstantSpell(const std::string& name)
{
	if(!hasLearnedInstantSpell(name))
		return;

	LearnedInstantSpellList::iterator it = std::find(learnedInstantSpellList.begin(), learnedInstantSpellList.end(), name);
	if(it != learnedInstantSpellList.end())
		learnedInstantSpellList.erase(it);
}

bool Player::hasLearnedInstantSpell(const std::string& name) const
{
	if(hasFlag(PlayerFlag_CannotUseSpells))
		return false;

	if(hasFlag(PlayerFlag_IgnoreSpellCheck))
		return true;

	for(LearnedInstantSpellList::const_iterator it = learnedInstantSpellList.begin(); it != learnedInstantSpellList.end(); ++it)
	{
		if(!strcasecmp((*it).c_str(), name.c_str()))
			return true;
	}

	return false;
}

void Player::manageAccount(const std::string &text)
{
	std::stringstream msg;
	msg << "Account Manager: ";

	bool noSwap = true;
	switch(accountManager)
	{
		case MANAGER_NAMELOCK:
		{
			if(!talkState[1])
			{
				managerString = text;
				trimString(managerString);
				if(managerString.length() < 4)
					msg << "Your name you want is too short, please select a longer name.";
				else if(managerString.length() > 20)
					msg << "The name you want is too long, please select a shorter name.";
				else if(!isValidName(managerString))
					msg << "That name seems to contain invalid symbols, please choose another name.";
				else if(IOLoginData::getInstance()->playerExists(managerString, true))
					msg << "A player with that name already exists, please choose another name.";
				else
				{
					std::string tmp = asLowerCaseString(managerString);
					if(tmp.substr(0, 4) != "god " && tmp.substr(0, 3) != "cm " && tmp.substr(0, 3) != "gm ")
					{
						talkState[1] = true;
						talkState[2] = true;
						msg << managerString << ", are you sure?";
					}
					else
						msg << "Your character is not a staff member, please tell me another name!";
				}
			}
			else if(checkText(text, "no") && talkState[2])
			{
				talkState[1] = talkState[2] = false;
				msg << "What else would you like to name your character?";
			}
			else if(checkText(text, "yes") && talkState[2])
			{
				if(!IOLoginData::getInstance()->playerExists(managerString, true))
				{
					uint32_t tmp;
					if(IOLoginData::getInstance()->getGuidByName(tmp, managerString2) &&
						IOLoginData::getInstance()->changeName(tmp, managerString, managerString2) &&
						IOBan::getInstance()->removePlayerBanishment(tmp, PLAYERBAN_LOCK))
					{
						if(House* house = Houses::getInstance()->getHouseByPlayerId(tmp))
							house->updateDoorDescription(managerString);

						talkState[1] = true;
						talkState[2] = false;
						msg << "Your character has been successfully renamed, you should now be able to login at it without any problems.";
					}
					else
					{
						talkState[1] = talkState[2] = false;
						msg << "Failed to change your name, please try again.";
					}
				}
				else
				{
					talkState[1] = talkState[2] = false;
					msg << "A player with that name already exists, please choose another name.";
				}
			}
			else
				msg << "Sorry, but I can't understand you, please try to repeat that!";

			break;
		}
		case MANAGER_ACCOUNT:
		{
			Account account = IOLoginData::getInstance()->loadAccount(managerNumber);
			if(checkText(text, "cancel") || (checkText(text, "account") && !talkState[1]))
			{
				talkState[1] = true;
				for(int8_t i = 2; i <= 12; i++)
					talkState[i] = false;

				msg << "Do you want to change your 'password', request a 'recovery key', add a 'character', or 'delete' a character?";
			}
			else if(checkText(text, "delete") && talkState[1])
			{
				talkState[1] = false;
				talkState[2] = true;
				msg << "Which character would you like to delete?";
			}
			else if(talkState[2])
			{
				std::string tmp = text;
				trimString(tmp);
				if(!isValidName(tmp, false))
					msg << "That name contains invalid characters, try to say your name again, you might have typed it wrong.";
				else
				{
					talkState[2] = false;
					talkState[3] = true;
					managerString = tmp;
					msg << "Do you really want to delete the character named " << managerString << "?";
				}
			}
			else if(checkText(text, "yes") && talkState[3])
			{
				switch(IOLoginData::getInstance()->deleteCharacter(managerNumber, managerString))
				{
					case DELETE_INTERNAL:
						msg << "An error occured while deleting your character. Either the character does not belong to you or it doesn't exist.";
						break;

					case DELETE_SUCCESS:
						msg << "Your character has been deleted.";
						break;

					case DELETE_HOUSE:
						msg << "Your character owns a house. To make sure you really want to lose your house by deleting your character, you have to login and leave the house or pass it to someone else first.";
						break;

					case DELETE_LEADER:
						msg << "Your character is the leader of a guild. You need to disband or pass the leadership someone else to delete your character.";
						break;

					case DELETE_ONLINE:
						msg << "A character with that name is currently online, to delete a character it has to be offline.";
						break;
				}

				talkState[1] = true;
				for(int8_t i = 2; i <= 12; i++)
					talkState[i] = false;
			}
			else if(checkText(text, "no") && talkState[3])
			{
				talkState[1] = true;
				talkState[3] = false;
				msg << "Tell me what character you want to delete.";
			}
			else if(checkText(text, "password") && talkState[1])
			{
				talkState[1] = false;
				talkState[4] = true;
				msg << "Tell me your new password please.";
			}
			else if(talkState[4])
			{
				std::string tmp = text;
				trimString(tmp);
				if(tmp.length() < 6)
					msg << "That password is too short, at least 6 digits are required. Please select a longer password.";
				else if(!isValidPassword(tmp))
					msg << "Your password contains invalid characters... please tell me another one.";
				else
				{
					talkState[4] = false;
					talkState[5] = true;
					managerString = tmp;
					msg << "Should '" << managerString << "' be your new password?";
				}
			}
			else if(checkText(text, "yes") && talkState[5])
			{
				talkState[1] = true;
				for(int8_t i = 2; i <= 12; i++)
					talkState[i] = false;

				IOLoginData::getInstance()->setPassword(managerNumber, managerString);
				msg << "Your password has been changed.";
			}
			else if(checkText(text, "no") && talkState[5])
			{
				talkState[1] = true;
				for(int8_t i = 2; i <= 12; i++)
					talkState[i] = false;

				msg << "Then not.";
			}
			else if(checkText(text, "character") && talkState[1])
			{
				uint32_t size = g_config.getNumber(ConfigManager::ACCOUNT_SIZE);
				if(account.charList.size() <= size)
				{
					talkState[1] = false;
					talkState[6] = true;
					msg << "What would you like as your character name?";
				}
				else
				{
					talkState[1] = true;
					for(int8_t i = 2; i <= 12; i++)
						talkState[i] = false;

					msg << "Your account reach the limit of " << g_config.getNumber(ConfigManager::ACCOUNT_SIZE) << " players, you can 'delete' a character if you want to create a new one.";
				}
			}
			else if(talkState[6])
			{
				managerString = text;
				trimString(managerString);
				if(managerString.length() < 4)
					msg << "Your name you want is too short, please select a longer name.";
				else if(managerString.length() > 20)
					msg << "The name you want is too long, please select a shorter name.";
				else if(!isValidName(managerString))
					msg << "That name seems to contain invalid symbols, please choose another name.";
				else if(IOLoginData::getInstance()->playerExists(managerString, true))
					msg << "A player with that name already exists, please choose another name.";
				else
				{
					std::string tmp = asLowerCaseString(managerString);
					if(tmp.substr(0, 4) != "god " && tmp.substr(0, 3) != "cm " && tmp.substr(0, 3) != "gm ")
					{
						talkState[6] = false;
						talkState[7] = true;
						msg << managerString << ", are you sure?";
					}
					else
						msg << "Your character is not a staff member, please tell me another name!";
				}
			}
			else if(checkText(text, "no") && talkState[7])
			{
				talkState[6] = true;
				talkState[7] = false;
				msg << "What else would you like to name your character?";
			}
			else if(checkText(text, "yes") && talkState[7])
			{
				talkState[7] = false;
				talkState[8] = true;
				msg << "Should your character be a 'male' or a 'female'.";
			}
			else if(talkState[8] && (checkText(text, "female") || checkText(text, "male")))
			{
				talkState[8] = false;
				talkState[9] = true;
				if(checkText(text, "female"))
				{
					msg << "A female, are you sure?";
					managerSex = PLAYERSEX_FEMALE;
				}
				else
				{
					msg << "A male, are you sure?";
					managerSex = PLAYERSEX_MALE;
				}
			}
			else if(checkText(text, "no") && talkState[9])
			{
				talkState[8] = true;
				talkState[9] = false;
				msg << "Tell me... would you like to be a 'male' or a 'female'?";
			}
			else if(checkText(text, "yes") && talkState[9])
			{
				if(g_config.getBool(ConfigManager::START_CHOOSEVOC))
				{
					talkState[9] = false;
					talkState[11] = true;

					bool firstPart = true;
					for(VocationsMap::iterator it = Vocations::getInstance()->getFirstVocation(); it != Vocations::getInstance()->getLastVocation(); ++it)
					{
						if(it->first == it->second->getFromVocation() && it->first != 0 && it->second->isManagerOption() == true)
						{
							if(firstPart)
							{
								msg << "What do you want to be... " << it->second->getDescription();
								firstPart = false;
							}
							else if(it->first - 1 != 0)
								msg << ", " << it->second->getDescription();
							else
								msg << " or " << it->second->getDescription() << ".";
						}
					}
				}
				else if(g_config.getBool(ConfigManager::START_CHOOSETOWN))
				{
					talkState[9] = false;
					talkState[13] = true;
					
					bool firstPart = true;
					for(TownMap::const_iterator it = Towns::getInstance()->getFirstTown(); it != Towns::getInstance()->getLastTown(); ++it)
					{
						if(it->second->getID() < 100)
						{
							if(firstPart)
							{
								msg << "Where do you want to live... " << it->second->getName();
								firstPart = false;
							}
							else if(it->first - 1 != 0)
								msg << ", " << it->second->getName();
							else
								msg << " or " << it->second->getName() << ".";
						}
					}
				}
				else if(!IOLoginData::getInstance()->playerExists(managerString, true))
				{
					talkState[1] = true;
					for(int8_t i = 2; i <= 12; i++)
						talkState[i] = false;

					if(IOLoginData::getInstance()->createCharacter(managerNumber, managerString, managerNumber2, (uint16_t)managerSex, managerNumber3))
						msg << "Your character has been created.";
					else
						msg << "Your character couldn't be created, please try again.";
				}
				else
				{
					talkState[6] = true;
					talkState[9] = false;
					msg << "A player with that name already exists, please choose another name.";
				}
			}
			else if(talkState[11])
			{
				for(VocationsMap::iterator it = Vocations::getInstance()->getFirstVocation(); it != Vocations::getInstance()->getLastVocation(); ++it)
				{
					std::string tmp = asLowerCaseString(it->second->getName());
					if(checkText(text, tmp) && it != Vocations::getInstance()->getLastVocation() && it->first == it->second->getFromVocation() && it->first != 0 && it->second->isManagerOption() == true)
					{
						msg << "So you would like to be " << it->second->getDescription() << "... are you sure?";
						managerNumber2 = it->first;
						talkState[11] = false;
						talkState[12] = true;
					}
				}

				if(msg.str().length() == 17)
					msg << "I don't understand what vocation you would like to be... could you please repeat it?";
			}
			else if(checkText(text, "yes") && talkState[12])
			{
				if(g_config.getBool(ConfigManager::START_CHOOSETOWN))
				{
					talkState[12] = false;
					talkState[13] = true;

					bool firstPart = true;
					for(TownMap::const_iterator it = Towns::getInstance()->getFirstTown(); it != Towns::getInstance()->getLastTown(); ++it)
					{
						if(it->second->getID() < 100)
						{
							if(firstPart)
							{
								msg << "Where do you want to live... " << it->second->getName();
								firstPart = false;
							}
							else if(it->first - 1 != 0)
								msg << ", " << it->second->getName();
							else
								msg << " or " << it->second->getName() << ".";
						}
					}
				}
				else if(!IOLoginData::getInstance()->playerExists(managerString, true))
				{
					talkState[1] = true;
					for(int8_t i = 2; i <= 12; i++)
						talkState[i] = false;

					if(IOLoginData::getInstance()->createCharacter(managerNumber, managerString, managerNumber2, (uint16_t)managerSex, managerNumber3))
						msg << "Your character has been created.";
					else
						msg << "Your character couldn't be created, please try again.";
				}
				else
				{
					talkState[6] = true;
					talkState[9] = false;
					msg << "A player with that name already exists, please choose another name.";
				}
			}
			else if(checkText(text, "no") && talkState[12])
			{
				talkState[11] = true;
				talkState[12] = false;
				msg << "No? Then what would you like to be?";
			}
			else if(talkState[13])
			{
				for(TownMap::const_iterator it = Towns::getInstance()->getFirstTown(); it != Towns::getInstance()->getLastTown(); it++)
				{
					std::string tmp = asLowerCaseString(it->second->getName());
					if(checkText(text, tmp) && it != Towns::getInstance()->getLastTown() && it->second->getID() < 100)
					{
						msg << "So do you want to live in " << it->second->getName() << ".. are you sure?";
						managerNumber3 = it->first;
						talkState[13] = false;
						talkState[14] = true;
					}
				}

				if(msg.str().length() == 17)
					msg << "I don't understand where you would like to live... could you please repeat it?";
			}
			else if(checkText(text, "yes") && talkState[14])
			{
				if(!IOLoginData::getInstance()->playerExists(managerString, true))
				{
					talkState[1] = true;
					for(int8_t i = 2; i <= 12; i++)
						talkState[i] = false;

					if(IOLoginData::getInstance()->createCharacter(managerNumber, managerString, managerNumber2, (uint16_t)managerSex, managerNumber3))
						msg << "Your character has been created.";
					else
						msg << "Your character couldn't be created, please try again.";
				}
				else
				{
					talkState[6] = true;
					talkState[9] = false;
					msg << "A player with that name already exists, please choose another name.";
				}
			}
			else if(checkText(text, "no") && talkState[14])
			{
				talkState[13] = true;
				talkState[14] = false;
				msg << "So where do you want to live?";
			}
			else if(checkText(text, "recovery key") && talkState[1])
			{
				talkState[1] = false;
				talkState[10] = true;
				msg << "Would you like a recovery key?";
			}
			else if(checkText(text, "yes") && talkState[10])
			{
				if(account.recoveryKey != "0")
					msg << "Sorry, you already have a recovery key, for security reasons I may not give you a new one.";
				else
				{
					managerString = generateRecoveryKey(4, 4);
					IOLoginData::getInstance()->setRecoveryKey(managerNumber, managerString);
					msg << "Your recovery key is: " << managerString << ".";
				}

				talkState[1] = true;
				for(int8_t i = 2; i <= 12; i++)
					talkState[i] = false;
			}
			else if(checkText(text, "no") && talkState[10])
			{
				msg << "Then not.";
				talkState[1] = true;
				for(int8_t i = 2; i <= 12; i++)
					talkState[i] = false;
			}
			else
				msg << "Please read the latest message that I have specified, I don't understand the current requested action.";

			break;
		}
		case MANAGER_NEW:
		{
			if(checkText(text, "account") && !talkState[1])
			{
				msg << "What would you like your password to be?";
				talkState[1] = true;
				talkState[2] = true;
			}
			else if(talkState[2])
			{
				std::string tmp = text;
				trimString(tmp);
				if(tmp.length() < 6)
					msg << "That password is too short, at least 6 digits are required. Please select a longer password.";
				else if(!isValidPassword(tmp))
					msg << "Your password contains invalid characters... please tell me another one.";
				else
				{
					talkState[3] = true;
					talkState[2] = false;
					managerString = tmp;
					msg << managerString << " is it? 'yes' or 'no'?";
				}
			}
			else if(checkText(text, "yes") && talkState[3])
			{
				if(g_config.getBool(ConfigManager::GENERATE_ACCOUNT_NUMBER))
				{
					do
						sprintf(managerChar, "%d%d%d%d%d%d%d", random_range(2, 9), random_range(2, 9), random_range(2, 9), random_range(2, 9), random_range(2, 9), random_range(2, 9), random_range(2, 9));
					while(IOLoginData::getInstance()->accountNameExists(managerChar));

					uint32_t id = (uint32_t)IOLoginData::getInstance()->createAccount(managerChar, managerString);
					if(id)
					{
						accountManager = MANAGER_ACCOUNT;
						managerNumber = id;

						noSwap = talkState[1] = false;
						msg << "Your account has been created, you may manage it now, but remember your account name: '"
							<< managerChar << "' and password: '" << managerString
							<< "'! If the account name is too hard to remember, please note it somewhere.";
					}
					else
						msg << "Your account could not be created, please try again.";

					for(int8_t i = 2; i <= 5; i++)
						talkState[i] = false;
				}
				else
				{
					msg << "What would you like your account name to be?";
					talkState[3] = false;
					talkState[4] = true;
				}
			}
			else if(checkText(text, "no") && talkState[3])
			{
				talkState[2] = true;
				talkState[3] = false;
				msg << "What would you like your password to be then?";
			}
			else if(talkState[4])
			{
				std::string tmp = text;
				trimString(tmp);
				if(tmp.length() < 3)
					msg << "That account name is too short, at least 3 digits are required. Please select a longer account name.";
				else if(tmp.length() > 25)
					msg << "That account name is too long, not more than 25 digits are required. Please select a shorter account name.";
				else if(!isValidAccountName(tmp))
					msg << "Your account name contains invalid characters, please choose another one.";
				else if(asLowerCaseString(tmp) == asLowerCaseString(managerString))
					msg << "Your account name cannot be same as password, please choose another one.";
				else
				{
					sprintf(managerChar, "%s", tmp.c_str());
					msg << managerChar << ", are you sure?";
					talkState[4] = false;
					talkState[5] = true;
				}
			}
			else if(checkText(text, "yes") && talkState[5])
			{
				if(!IOLoginData::getInstance()->accountNameExists(managerChar))
				{
					uint32_t id = (uint32_t)IOLoginData::getInstance()->createAccount(managerChar, managerString);
					if(id)
					{
						accountManager = MANAGER_ACCOUNT;
						managerNumber = id;

						noSwap = talkState[1] = false;
						msg << "Your account has been created, you may manage it now, but remember your account name: '"
							<< managerChar << "' and password: '" << managerString << "'!";
					}
					else
						msg << "Your account could not be created, please try again.";

					for(int8_t i = 2; i <= 5; i++)
						talkState[i] = false;
				}
				else
				{
					msg << "An account with that name already exists, please try another account name.";
					talkState[4] = true;
					talkState[5] = false;
				}
			}
			else if(checkText(text, "no") && talkState[5])
			{
				talkState[5] = false;
				talkState[4] = true;
				msg << "What else would you like as your account name?";
			}
			else if(checkText(text, "recover") && !talkState[6])
			{
				talkState[6] = true;
				talkState[7] = true;
				msg << "What was your account name?";
			}
			else if(talkState[7])
			{
				managerString = text;
				if(IOLoginData::getInstance()->getAccountId(managerString, (uint32_t&)managerNumber))
				{
					talkState[7] = false;
					talkState[8] = true;
					msg << "What was your recovery key?";
				}
				else
				{
					msg << "Sorry, but account with such name doesn't exists.";
					talkState[6] = talkState[7] = false;
				}
			}
			else if(talkState[8])
			{
				managerString2 = text;
				if(IOLoginData::getInstance()->validRecoveryKey(managerNumber, managerString2) && managerString2 != "0")
				{
					sprintf(managerChar, "%s%d", g_config.getString(ConfigManager::SERVER_NAME).c_str(), random_range(100, 999));
					IOLoginData::getInstance()->setPassword(managerNumber, managerChar);
					msg << "Correct! Your new password is: " << managerChar << ".";
				}
				else
					msg << "Sorry, but this key doesn't match to account you gave me.";

				talkState[7] = talkState[8] = false;
			}
			else
				msg << "Sorry, but I can't understand you, please try to repeat that.";

			break;
		}
		default:
			return;
			break;
	}

	sendTextMessage(MSG_STATUS_CONSOLE_BLUE, msg.str().c_str());
	if(!noSwap)
		sendTextMessage(MSG_STATUS_CONSOLE_ORANGE, "Hint: Type 'account' to manage your account and if you want to start over then type 'cancel'.");
}

bool Player::isGuildInvited(uint32_t guildId) const
{
	for(InvitedToGuildsList::const_iterator it = invitedToGuildsList.begin(); it != invitedToGuildsList.end(); ++it)
	{
		if((*it) == guildId)
			return true;
	}

	return false;
}

void Player::leaveGuild()
{
	sendClosePrivate(CHANNEL_GUILD);
#ifdef __WAR_SYSTEM__
	warMap.clear();
	g_game.updateCreatureEmblem(this);

#endif
	guildLevel = GUILDLEVEL_NONE;
	guildId = rankId = 0;
	guildName = rankName = guildNick = std::string();
}

bool Player::isPremium() const
{
	if(g_config.getBool(ConfigManager::FREE_PREMIUM) || hasFlag(PlayerFlag_IsAlwaysPremium))
		return true;

	return premiumDays;
}

bool Player::setGuildLevel(GuildLevel_t newLevel, uint32_t rank/* = 0*/)
{
	std::string name;
	if(!IOGuild::getInstance()->getRankEx(rank, name, guildId, newLevel))
		return false;

	guildLevel = newLevel;
	rankName = name;
	rankId = rank;
	return true;
}

void Player::setGroupId(int32_t newId)
{
	if(Group* tmp = Groups::getInstance()->getGroup(newId))
	{
		groupId = newId;
		group = tmp;
	}
}

void Player::setGroup(Group* newGroup)
{
	if(!newGroup)
		return;

	group = newGroup;
	groupId = group->getId();
}

PartyShields_t Player::getPartyShield(const Creature* creature) const
{
	const Player* player = creature->getPlayer();
	if(!player)
		return Creature::getPartyShield(creature);

	if(Party* party = getParty())
	{
		if(party->getLeader() == player)
		{
			if(!party->isSharedExperienceActive())
				return SHIELD_YELLOW;

			if(party->isSharedExperienceEnabled())
				return SHIELD_YELLOW_SHAREDEXP;

			if(party->canUseSharedExperience(player))
				return SHIELD_YELLOW_NOSHAREDEXP;

			return SHIELD_YELLOW_NOSHAREDEXP_BLINK;
		}

		if(party->isPlayerMember(player))
		{
			if(!party->isSharedExperienceActive())
				return SHIELD_BLUE;

			if(party->isSharedExperienceEnabled())
				return SHIELD_BLUE_SHAREDEXP;

			if(party->canUseSharedExperience(player))
				return SHIELD_BLUE_NOSHAREDEXP;

			return SHIELD_BLUE_NOSHAREDEXP_BLINK;
		}

		if(isInviting(player))
			return SHIELD_WHITEBLUE;
	}

	if(player->isInviting(this))
		return SHIELD_WHITEYELLOW;

	return SHIELD_NONE;
}

bool Player::isInviting(const Player* player) const
{
	if(!player || player->isRemoved() || !party || party->getLeader() != this)
		return false;

	return party->isPlayerInvited(player);
}

bool Player::isPartner(const Player* player) const
{
	return player && player->getParty() && player->getParty() == party;
}

bool Player::getHideHealth() const
{
	if(hasFlag(PlayerFlag_HideHealth))
		return true;

	return hideHealth;
}

void Player::sendPlayerIcons(Player* player)
{
	sendCreatureShield(player);
	sendCreatureSkull(player);
}

bool Player::addPartyInvitation(Party* party)
{
	if(!party)
		return false;

	PartyList::iterator it = std::find(invitePartyList.begin(), invitePartyList.end(), party);
	if(it != invitePartyList.end())
		return false;

	invitePartyList.push_back(party);
	return true;
}

bool Player::removePartyInvitation(Party* party)
{
	if(!party)
		return false;

	PartyList::iterator it = std::find(invitePartyList.begin(), invitePartyList.end(), party);
	if(it != invitePartyList.end())
	{
		invitePartyList.erase(it);
		return true;
	}
	return false;
}

void Player::clearPartyInvitations()
{
	if(invitePartyList.empty())
		return;

	PartyList list;
	for(PartyList::iterator it = invitePartyList.begin(); it != invitePartyList.end(); ++it)
		list.push_back(*it);

	invitePartyList.clear();
	for(PartyList::iterator it = list.begin(); it != list.end(); ++it)
		(*it)->removeInvite(this);
}

void Player::increaseCombatValues(int32_t& min, int32_t& max, bool useCharges, bool countWeapon)
{
	if(min > 0)
		min = (int32_t)(min * vocation->getMultiplier(MULTIPLIER_HEALING));
	else
		min = (int32_t)(min * vocation->getMultiplier(MULTIPLIER_MAGIC));

	if(max > 0)
		max = (int32_t)(max * vocation->getMultiplier(MULTIPLIER_HEALING));
	else
		max = (int32_t)(max * vocation->getMultiplier(MULTIPLIER_MAGIC));

	Item* item = NULL;
	int32_t minValue = 0, maxValue = 0, i = SLOT_FIRST;
	for(; i < SLOT_LAST; ++i)
	{
		if(!(item = getInventoryItem((slots_t)i)) || item->isRemoved() ||
			(g_moveEvents->hasEquipEvent(item) && !isItemAbilityEnabled((slots_t)i)))
			continue;

		const ItemType& it = Item::items[item->getID()];
		if(min > 0)
		{
			minValue += it.abilities.increment[HEALING_VALUE];
			if(it.abilities.increment[HEALING_PERCENT])
				min = (int32_t)std::ceil((double)(min * it.abilities.increment[HEALING_PERCENT]) / 100.);
		}
		else
		{
			minValue -= it.abilities.increment[MAGIC_VALUE];
			if(it.abilities.increment[MAGIC_PERCENT])
				min = (int32_t)std::ceil((double)(min * it.abilities.increment[MAGIC_PERCENT]) / 100.);
		}

		if(max > 0)
		{
			maxValue += it.abilities.increment[HEALING_VALUE];
			if(it.abilities.increment[HEALING_PERCENT])
				max = (int32_t)std::ceil((double)(max * it.abilities.increment[HEALING_PERCENT]) / 100.);
		}
		else
		{
			maxValue -= it.abilities.increment[MAGIC_VALUE];
			if(it.abilities.increment[MAGIC_PERCENT])
				max = (int32_t)std::ceil((double)(max * it.abilities.increment[MAGIC_PERCENT]) / 100.);
		}

		bool removeCharges = false;
		for(int32_t j = INCREMENT_FIRST; j <= INCREMENT_LAST; ++j)
		{
			if(!it.abilities.increment[(Increment_t)j])
				continue;

			removeCharges = true;
			break;
		}

		if(useCharges && removeCharges && (countWeapon || item != weapon) && item->hasCharges())
			g_game.transformItem(item, item->getID(), std::max((int32_t)0, (int32_t)item->getCharges() - 1));
	}

	min += minValue;
	max += maxValue;
}

bool Player::transferMoneyTo(const std::string& name, uint64_t amount)
{
	if(!g_config.getBool(ConfigManager::BANK_SYSTEM) || amount > balance)
		return false;

	Player* target = g_game.getPlayerByNameEx(name);
	if(!target)
		return false;

	balance -= amount;
	target->balance += amount;
	if(target->isVirtual())
	{
		IOLoginData::getInstance()->savePlayer(target);
		delete target;
	}

	return true;
}

void Player::sendCritical() const
{
	if(g_config.getBool(ConfigManager::DISPLAY_CRITICAL_HIT))
		g_game.addAnimatedText(getPosition(), COLOR_DARKRED, "CRITICAL!");
}

void Player::setPlayerExtraAttackSpeed(uint32_t speed)
{
	extraAttackSpeed = speed;
}
