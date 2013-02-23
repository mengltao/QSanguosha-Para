#include "thicket.h"
#include "general.h"
#include "skill.h"
#include "room.h"
#include "maneuvering.h"
#include "clientplayer.h"
#include "client.h"
#include "engine.h"
#include "general.h"

class Xingshang: public TriggerSkill {
public:
    Xingshang(): TriggerSkill("xingshang") {
        events << Death;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *caopi, QVariant &data) const{
        DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *player = death.who;
        if (player->isNude() || caopi == player)
            return false;
        if (caopi->isAlive() && room->askForSkillInvoke(caopi, objectName(), data)) {
            room->broadcastSkillInvoke(objectName());

            DummyCard *dummy = new DummyCard;
            QList <const Card *> handcards = player->getHandcards();
            foreach (const Card *card, handcards)
                dummy->addSubcard(card);

            QList <const Card *> equips = player->getEquips();
            foreach (const Card *card, equips)
                dummy->addSubcard(card);

            if (dummy->subcardsLength() > 0) {
                CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, caopi->objectName());
                room->obtainCard(caopi, dummy, reason, false);
            }
            delete dummy;
        }

        return false;
    }
};

FangzhuCard::FangzhuCard() {
    mute = true;
}

void FangzhuCard::onEffect(const CardEffectStruct &effect) const{
    int x = effect.from->getLostHp();
    effect.to->drawCards(x);

    Room *room = effect.to->getRoom();
    if (effect.from->hasInnateSkill("fangzhu") || !effect.from->hasSkill("jilve"))
        room->broadcastSkillInvoke("fangzhu", effect.to->faceUp() ? 1 : 2);
    else
        room->broadcastSkillInvoke("jilve", 2);

    effect.to->turnOver();
}

class FangzhuViewAsSkill: public ZeroCardViewAsSkill {
public:
    FangzhuViewAsSkill(): ZeroCardViewAsSkill("fangzhu") {
    }

    virtual bool isEnabledAtPlay(const Player *) const{
        return false;
    }

    virtual bool isEnabledAtResponse(const Player *, const QString &pattern) const{
        return pattern == "@@fangzhu";
    }

    virtual const Card *viewAs() const{
        return new FangzhuCard;
    }
};

class Fangzhu: public MasochismSkill {
public:
    Fangzhu(): MasochismSkill("fangzhu") {
        view_as_skill = new FangzhuViewAsSkill;
    }

    virtual void onDamaged(ServerPlayer *caopi, const DamageStruct &damage) const{
        Room *room = caopi->getRoom();
        room->askForUseCard(caopi, "@@fangzhu", "@fangzhu");
    }
};

class Songwei: public TriggerSkill {
public:
    Songwei(): TriggerSkill("songwei$") {
        events << FinishJudge;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL && target->getKingdom() == "wei";
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        JudgeStar judge = data.value<JudgeStar>();
        CardStar card = judge->card;

        if (card->isBlack()) {
            QList<ServerPlayer *> caopis;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->hasLordSkill(objectName()))
                    caopis << p;
            }
            
            while (!caopis.isEmpty()) {
                if (player->askForSkillInvoke(objectName())) {
                    ServerPlayer *caopi = room->askForPlayerChosen(player, caopis, objectName());
                    room->broadcastSkillInvoke(objectName());
                    room->notifySkillInvoked(caopi, objectName());
                    LogMessage log;
                    log.type = "#InvokeOthersSkill";
                    log.from = player;
                    log.to << caopi;
                    log.arg = objectName();
                    room->sendLog(log);

                    caopi->drawCards(1);
                    caopi->setFlags("songwei_used"); //for AI
                    caopis.removeOne(caopi);
                } else
                    break;
            }
                    
            foreach (ServerPlayer *caopi, room->getAllPlayers())
                caopi->setFlags("-songwei_used");
        }

        return false;
    }
};

class Duanliang: public OneCardViewAsSkill {
public:
    Duanliang(): OneCardViewAsSkill("duanliang") {
    }

    virtual bool viewFilter(const Card *card) const{
        return card->isBlack() && (card->isKindOf("BasicCard") || card->isKindOf("EquipCard"));
    }

    virtual const Card *viewAs(const Card *originalCard) const{
        SupplyShortage *shortage = new SupplyShortage(originalCard->getSuit(), originalCard->getNumber());
        shortage->setSkillName(objectName());
        shortage->addSubcard(originalCard);

        return shortage;
    }
};

class DuanliangTargetMod: public TargetModSkill {
public:
    DuanliangTargetMod(): TargetModSkill("#duanliang-target") {
        frequency = NotFrequent;
        pattern = "SupplyShortage";
    }

    virtual int getDistanceLimit(const Player *from, const Card *) const{
        if (from->hasSkill("duanliang"))
            return 1;
        else
            return 0;
    }
};

class SavageAssaultAvoid: public TriggerSkill {
public:
    SavageAssaultAvoid(const QString &avoid_skill)
        : TriggerSkill("#sa_avoid_" + avoid_skill), avoid_skill(avoid_skill)
    {
        events << CardEffected;
    }

    virtual bool trigger(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const{
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.card->isKindOf("SavageAssault")) {
            room->broadcastSkillInvoke(avoid_skill);

            LogMessage log;
            log.type = "#SkillNullify";
            log.from = player;
            log.arg = avoid_skill;
            log.arg2 = "savage_assault";
            room->sendLog(log);

            return true;
        } else
            return false;
    }

private:
    QString avoid_skill;
};

class Huoshou: public TriggerSkill {
public:
    Huoshou(): TriggerSkill("huoshou") {
        events << TargetConfirmed << ConfirmDamage << CardFinished;
        frequency = Compulsory;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == TargetConfirmed && player->hasSkill(objectName()) && player->isAlive()) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SavageAssault") && use.from != player) {
                room->notifySkillInvoked(player, objectName());
                room->broadcastSkillInvoke(objectName());
                room->setTag("HuoshouSource", QVariant::fromValue((PlayerStar)player));
            }
        } else if (triggerEvent == ConfirmDamage && !room->getTag("HuoshouSource").isNull()) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("SavageAssault"))
                return false;

            ServerPlayer *menghuo = room->getTag("HuoshouSource").value<PlayerStar>();
            damage.from = menghuo->isAlive() ? menghuo : NULL;
            data = QVariant::fromValue(damage);
        } else if (triggerEvent == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SavageAssault"))
                room->removeTag("HuoshouSource");
        }

        return false;
    }
};

class Lieren: public TriggerSkill {
public:
    Lieren(): TriggerSkill("lieren") {
        events << Damage;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *zhurong, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *target = damage.to;
        if (damage.card && damage.card->isKindOf("Slash") && !zhurong->isKongcheng()
            && !target->isKongcheng() && target != zhurong && !damage.chain && !damage.transfer
            && room->askForSkillInvoke(zhurong, objectName(), data)) {
            room->broadcastSkillInvoke(objectName(), 1);

            bool success = zhurong->pindian(target, "lieren", NULL);
            if (!success) return false;

            room->broadcastSkillInvoke(objectName(), 2);
            if (!target->isNude()) {
                int card_id = room->askForCardChosen(zhurong, target, "he", objectName());
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, zhurong->objectName());
                room->obtainCard(zhurong, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);
            }
        }

        return false;
    }
};

class Zaiqi: public PhaseChangeSkill {
public:
    Zaiqi(): PhaseChangeSkill("zaiqi") {
    }

    virtual bool onPhaseChange(ServerPlayer *menghuo) const{
        if (menghuo->getPhase() == Player::Draw && menghuo->isWounded()) {
            Room *room = menghuo->getRoom();
            if (room->askForSkillInvoke(menghuo, objectName())) {
                room->broadcastSkillInvoke(objectName(), 1);

                bool has_heart = false;
                int x = menghuo->getLostHp();
                QList<int> ids = room->getNCards(x, false);
                CardsMoveStruct move;
                move.card_ids = ids;
                move.to = menghuo;
                move.to_place = Player::PlaceTable;
                move.reason = CardMoveReason(CardMoveReason::S_REASON_TURNOVER, menghuo->objectName(), "zaiqi", QString());
                room->moveCardsAtomic(move, true);

                room->getThread()->delay();
                room->getThread()->delay();

                QList<int> card_to_throw;
                QList<int> card_to_gotback;
                for (int i = 0; i < x; i++) {
                    if (Sanguosha->getCard(ids[i])->getSuit() == Card::Heart)
                        card_to_throw << ids[i];
                    else
                        card_to_gotback << ids[i];
                }
                if (!card_to_throw.isEmpty()) {
                    DummyCard *dummy = new DummyCard;
                    foreach (int id, card_to_throw)
                        dummy->addSubcard(id);

                    RecoverStruct recover;
                    recover.card = dummy;
                    recover.who = menghuo;
                    recover.recover = card_to_throw.length();
                    room->recover(menghuo, recover);

                    CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, menghuo->objectName(), "zaiqi", QString());
                    room->throwCard(dummy, reason, NULL);
                    dummy->deleteLater();
                    has_heart = true;
                }
                if (!card_to_gotback.isEmpty()) {
                    DummyCard *dummy2 = new DummyCard;
                    foreach (int id, card_to_gotback)
                        dummy2->addSubcard(id);

                    CardMoveReason reason(CardMoveReason::S_REASON_GOTBACK, menghuo->objectName());
                    room->obtainCard(menghuo, dummy2, reason);
                    dummy2->deleteLater();
                }

                if (has_heart)
                    room->broadcastSkillInvoke(objectName(), 2);

                return true;
            }
        }

        return false;
    }
};

class Juxiang: public TriggerSkill {
public:
    Juxiang(): TriggerSkill("juxiang") {
        events << CardUsed << CardsMoving;
        frequency = Compulsory;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const{
        if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isVirtualCard()) {
                if (use.card->isKindOf("SavageAssault")
                    && use.card->subcardsLength() == 1
                    && Sanguosha->getCard(use.card->getSubcards().first())->isKindOf("SavageAssault"))
                    room->setCardFlag(use.card->getSubcards().first(), "real_SA");
            } else if (use.card->isKindOf("SavageAssault")) {
                room->setCardFlag(use.card->getId(), "real_SA");
            }
        } else if (TriggerSkill::triggerable(player)) {
            CardsMoveOneTimeStar move = data.value<CardsMoveOneTimeStar>();
            if (move->card_ids.length() == 1 && move->from_places.contains(Player::PlaceTable) && move->to_place == Player::DiscardPile
                && move->reason.m_reason == CardMoveReason::S_REASON_USE) {
                Card *card = Sanguosha->getCard(move->card_ids.first());
                if (card->hasFlag("real_SA") && player != move->from) {
                    room->notifySkillInvoked(player, objectName());
                    room->broadcastSkillInvoke(objectName());
                    player->obtainCard(card);
                }
            }
        }

        return false;
    }
};

YinghunCard::YinghunCard() {
    mute = true;
}

void YinghunCard::onEffect(const CardEffectStruct &effect) const{
    int x = effect.from->getLostHp();

    int index = 1;
    if (!effect.from->hasInnateSkill("yinghun") && effect.from->hasSkill("hunzi"))
        index += 2;
    Room *room = effect.from->getRoom();

    if (x == 1) {
        room->broadcastSkillInvoke("yinghun", index);

        effect.to->drawCards(1);
        room->askForDiscard(effect.to, "yinghun", 1, 1, false, true);
    } else {
        effect.to->setFlags("YinghunTarget");
        QString choice = room->askForChoice(effect.from, "yinghun", "d1tx+dxt1");
        effect.to->setFlags("-YinghunTarget");
        if (choice == "d1tx") {
            room->broadcastSkillInvoke("yinghun", index + 1);

            effect.to->drawCards(1);
            room->askForDiscard(effect.to, "yinghun", x, x, false, true);
        } else {
            room->broadcastSkillInvoke("yinghun", index);

            effect.to->drawCards(x);
            room->askForDiscard(effect.to, "yinghun", 1, 1, false, true);
        }
    }
}

class YinghunViewAsSkill: public ZeroCardViewAsSkill {
public:
    YinghunViewAsSkill(): ZeroCardViewAsSkill("yinghun") {
    }

    virtual const Card *viewAs() const{
        return new YinghunCard;
    }

    virtual bool isEnabledAtPlay(const Player *) const{
        return false;
    }

    virtual bool isEnabledAtResponse(const Player *, const QString &pattern) const{
        return pattern == "@@yinghun";
    }
};

class Yinghun: public PhaseChangeSkill {
public:
    Yinghun(): PhaseChangeSkill("yinghun") {
        default_choice = "d1tx";
        view_as_skill = new YinghunViewAsSkill;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return PhaseChangeSkill::triggerable(target)
               && target->getPhase() == Player::Start
               && target->isWounded();
    }

    virtual bool onPhaseChange(ServerPlayer *sunjian) const{
        Room *room = sunjian->getRoom();
        room->askForUseCard(sunjian, "@@yinghun", "@yinghun");

        return false;
    }
};

HaoshiCard::HaoshiCard() {
    will_throw = false;
    mute = true;
    handling_method = Card::MethodNone;
}

bool HaoshiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    if (!targets.isEmpty() || to_select == Self)
        return false;

    return to_select->getHandcardNum() == Self->getMark("haoshi");
}

void HaoshiCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const{
    room->moveCardTo(this, targets.first(), Player::PlaceHand, false);
}

class HaoshiViewAsSkill: public ViewAsSkill {
public:
    HaoshiViewAsSkill(): ViewAsSkill("haoshi") {
    }

    virtual bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const{
        if (to_select->isEquipped())
            return false;

        int length = Self->getHandcardNum() / 2;
        return selected.length() < length;
    }

    virtual const Card *viewAs(const QList<const Card *> &cards) const{
        if (cards.length() != Self->getHandcardNum() / 2)
            return NULL;

        HaoshiCard *card = new HaoshiCard;
        card->addSubcards(cards);
        return card;
    }

    virtual bool isEnabledAtPlay(const Player *) const{
        return false;
    }

    virtual bool isEnabledAtResponse(const Player *, const QString &pattern) const{
        return pattern == "@@haoshi!";
    }
};

class HaoshiGive: public TriggerSkill {
public:
    HaoshiGive(): TriggerSkill("#haoshi-give") {
        events << AfterDrawNCards;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *lusu, QVariant &) const{
        if (lusu->hasFlag("haoshi")) {
            lusu->setFlags("-haoshi");

            if (lusu->getHandcardNum() <= 5)
                return false;            

            QList<ServerPlayer *> other_players = room->getOtherPlayers(lusu);
            int least = 1000;
            foreach (ServerPlayer *player, other_players)
                least = qMin(player->getHandcardNum(), least);
            room->setPlayerMark(lusu, "haoshi", least);
            bool used = room->askForUseCard(lusu, "@@haoshi!", "@haoshi", -1, Card::MethodNone);

            if (!used) {
                // force lusu to give his half cards
                ServerPlayer *beggar = NULL;
                foreach (ServerPlayer *player, other_players) {
                    if (player->getHandcardNum() == least) {
                        beggar = player;
                        break;
                    }
                }

                int n = lusu->getHandcardNum() / 2;
                QList<int> to_give = lusu->handCards().mid(0, n);
                HaoshiCard *haoshi_card = new HaoshiCard;
                foreach (int card_id, to_give)
                    haoshi_card->addSubcard(card_id);
                QList<ServerPlayer *> targets;
                targets << beggar;
                haoshi_card->use(room, lusu, targets);
                delete haoshi_card;
            }
        }

        return false;
    }
};

class Haoshi: public DrawCardsSkill {
public:
    Haoshi(): DrawCardsSkill("#haoshi") {
    }

    virtual int getDrawNum(ServerPlayer *lusu, int n) const{
        Room *room = lusu->getRoom();
        if (room->askForSkillInvoke(lusu, "haoshi")) {
            room->broadcastSkillInvoke("haoshi");
            lusu->setFlags("haoshi");
            return n + 2;
        } else
            return n;
    }
};

DimengCard::DimengCard() {
}

bool DimengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    if (to_select == Self)
        return false;

    if (targets.isEmpty())
        return true;

    if (targets.length() == 1) {
        int max_diff = Self->getCardCount(true);
        return max_diff >= qAbs(to_select->getHandcardNum() - targets.first()->getHandcardNum());
    }

    return false;
}

bool DimengCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const{
    return targets.length() == 2;
}

const Card *DimengCard::validate(const CardUseStruct *card_use) const{
    ServerPlayer *player = card_use->from;

    ServerPlayer *a = card_use->to.at(0);
    ServerPlayer *b = card_use->to.at(1);

    int n1 = a->getHandcardNum();
    int n2 = b->getHandcardNum();

    int diff = qAbs(n1 - n2);
    int to_discard = 0;

    foreach (const Card *card, player->getHandcards()) {
        if (!player->isJilei(card))
            to_discard++;
    }
    foreach (const Card *card, player->getEquips()) {
        if (!player->isJilei(card))
            to_discard++;
    }
    if (to_discard < diff) return NULL;
    return this;
}

void DimengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const{
    ServerPlayer *a = targets.at(0);
    ServerPlayer *b = targets.at(1);

    int n1 = a->getHandcardNum();
    int n2 = b->getHandcardNum();

    int diff = qAbs(n1 - n2);
    if (diff != 0)
        room->askForDiscard(source, "dimeng", diff, diff, false, true);

    QList<CardsMoveStruct> exchangeMove;
    CardsMoveStruct move1;
    move1.card_ids = a->handCards();
    move1.to = b;
    move1.to_place = Player::PlaceHand;
    CardsMoveStruct move2;
    move2.card_ids = b->handCards();
    move2.to = a;
    move2.to_place = Player::PlaceHand;
    exchangeMove.push_back(move1);
    exchangeMove.push_back(move2);
    room->moveCards(exchangeMove, false);

    LogMessage log;
    log.type = "#Dimeng";
    log.from = a;
    log.to << b;
    log.arg = QString::number(n1);
    log.arg2 = QString::number(n2);
    room->sendLog(log);
    room->getThread()->delay();
}

class Dimeng: public ZeroCardViewAsSkill {
public:
    Dimeng(): ZeroCardViewAsSkill("dimeng") {
    }

    virtual const Card *viewAs() const{
        return new DimengCard;
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        return !player->hasUsed("DimengCard");
    }
};

class Wansha: public TriggerSkill {
public:
    Wansha(): TriggerSkill("wansha") {
        frequency = Compulsory;
        events << PreAskForPeaches;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        ServerPlayer *jiaxu = target->getRoom()->getCurrent();
        return jiaxu && jiaxu->hasSkill(objectName()) && jiaxu->isAlive() && target->getHp() <= 0;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        DyingStruct dying = data.value<DyingStruct>();
        ServerPlayer *jiaxu = room->getCurrent();
        if (jiaxu->hasInnateSkill("wansha") || !jiaxu->hasSkill("jilve"))
            room->broadcastSkillInvoke(objectName());
        else
            room->broadcastSkillInvoke("jilve", 3);

        QList<ServerPlayer *> savers;
        savers << jiaxu;

        LogMessage log;
        log.from = jiaxu;
        log.arg = objectName();
        if (jiaxu != player) {
            savers << player;
            log.type = "#WanshaTwo";
            log.to << player;
        } else {
            log.type = "#WanshaOne";
        }
        room->sendLog(log);
        dying.savers = savers;

        data = QVariant::fromValue(dying);
        return false;
    }
};

class WanshaPrevent: public TriggerSkill {
public:
    WanshaPrevent(): TriggerSkill("#wansha-prevent") {
        events << AskForPeaches;
    }

    virtual int getPriority() const{
        return 1;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        ServerPlayer *jiaxu = target->getRoom()->getCurrent();
        return jiaxu && jiaxu->hasSkill("wansha") && jiaxu->isAlive();
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        DyingStruct dying = data.value<DyingStruct>();
        ServerPlayer *current = room->getCurrent();
        if (dying.who != player && current != player)
            return true;
        return false;
    }
};

class Luanwu: public ZeroCardViewAsSkill {
public:
    Luanwu(): ZeroCardViewAsSkill("luanwu") {
        frequency = Limited;
    }

    virtual const Card *viewAs() const{
        return new LuanwuCard;
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        return player->getMark("@chaos") >= 1;
    }
};

LuanwuCard::LuanwuCard() {
    mute = true;
    target_fixed = true;
}

void LuanwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const{
    source->loseMark("@chaos");
    room->broadcastSkillInvoke("luanwu");
    QString lightbox = "$LuanwuAnimate";
    if (source->getGeneralName() != "jiaxu" && (source->getGeneralName() == "sp_jiaxu" || source->getGeneral2Name() == "sp_jiaxu"))
        lightbox = lightbox + "SP";
    room->doLightbox(lightbox, 3000);

    QList<ServerPlayer *> players = room->getOtherPlayers(source);
    foreach (ServerPlayer *player, players) {
        if (player->isAlive())
            room->cardEffect(this, source, player);
            room->getThread()->delay();
    }
}

void LuanwuCard::onEffect(const CardEffectStruct &effect) const{
    Room *room = effect.to->getRoom();

    QList<ServerPlayer *> players = room->getOtherPlayers(effect.to);
    QList<int> distance_list;
    int nearest = 1000;
    foreach (ServerPlayer *player, players) {
        int distance = effect.to->distanceTo(player);
        distance_list << distance;
        nearest = qMin(nearest, distance);
    }

    QList<ServerPlayer *> luanwu_targets;
    for (int i = 0; i < distance_list.length(); i++) {
        if (distance_list[i] == nearest && effect.to->canSlash(players[i], NULL, false))
            luanwu_targets << players[i];
    }

    if (luanwu_targets.isEmpty() || !room->askForUseSlashTo(effect.to, luanwu_targets, "@luanwu-slash"))
        room->loseHp(effect.to);
}

class Weimu: public ProhibitSkill {
public:
    Weimu(): ProhibitSkill("weimu") {
    }

    virtual bool isProhibited(const Player *, const Player *, const Card *card) const{
        return (card->isKindOf("TrickCard") || card->isKindOf("QiceCard"))
               && card->isBlack() && card->getSkillName() != "guhuo"; // Be care!!!!!!
    }
};

class Jiuchi: public OneCardViewAsSkill {
public:
    Jiuchi(): OneCardViewAsSkill("jiuchi") {
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        return Analeptic::IsAvailable(player);
    }

    virtual bool isEnabledAtResponse(const Player *, const QString &pattern) const{
        return  pattern.contains("analeptic");
    }

    virtual bool viewFilter(const Card *to_select) const{
        return !to_select->isEquipped() && to_select->getSuit() == Card::Spade;
    }

    virtual const Card *viewAs(const Card *originalCard) const{
        Analeptic *analeptic = new Analeptic(originalCard->getSuit(), originalCard->getNumber());
        analeptic->setSkillName(objectName());
        analeptic->addSubcard(originalCard->getId());
        return analeptic;
    }
};

class Roulin: public TriggerSkill {
public:
    Roulin(): TriggerSkill("roulin") {
        events << TargetConfirmed << CardFinished;
        frequency = Compulsory;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL && (target->hasSkill(objectName()) || target->isFemale());
    }

    virtual bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const{
        if (event == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash") && player == use.from) {
                int mark_n = player->getMark("double_jink" + use.card->toString());
                int count = 0;
                bool play_effect = false;
                if (TriggerSkill::triggerable(use.from)) {
                    count = 1;
                    foreach (ServerPlayer *p, use.to) {
                        if (p->isFemale()) {
                            play_effect = true;
                            mark_n += count;
                            room->setPlayerMark(use.from, "double_jink" + use.card->toString(), mark_n);
                        }
                        count *= 10;
                    }
                    if (play_effect) {
                        LogMessage log;
                        log.from = use.from;
                        log.arg = objectName();
                        log.type = "#TriggerSkill";
                        room->sendLog(log);
                        room->notifySkillInvoked(use.from, objectName());

                        room->broadcastSkillInvoke(objectName(), 1);
                    }
                } else if (use.from->isFemale()) {
                    count = 1;
                    foreach (ServerPlayer *p, use.to) {
                        if (p->hasSkill(objectName())) {
                            play_effect = true;
                            mark_n += count;
                            room->setPlayerMark(use.from, "double_jink" + use.card->toString(), mark_n);
                        }
                        count *= 10;
                    }
                    if (play_effect) {
                        foreach (ServerPlayer *p, use.to) {
                            if (p->hasSkill(objectName())) {
                                LogMessage log;
                                log.from = p;
                                log.arg = objectName();
                                log.type = "#TriggerSkill";
                                room->sendLog(log);
                                room->notifySkillInvoked(p, objectName());
                            }
                        }
                        room->broadcastSkillInvoke(objectName(), 2);
                    }
                }
            }
        } else if (event == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash"))
                room->setPlayerMark(use.from, "double_jink" + use.card->toString(), 0);
        }

        return false;
    }
};

class Benghuai: public PhaseChangeSkill {
public:
    Benghuai(): PhaseChangeSkill("benghuai") {
        frequency = Compulsory;
    }

    virtual bool onPhaseChange(ServerPlayer *dongzhuo) const{
        bool trigger_this = false;
        Room *room = dongzhuo->getRoom();

        if (dongzhuo->getPhase() == Player::Finish) {
            QList<ServerPlayer *> players = room->getOtherPlayers(dongzhuo);
            foreach (ServerPlayer *player, players) {
                if (dongzhuo->getHp() > player->getHp()) {
                    trigger_this = true;
                    break;
                }
            }
        }

        if (trigger_this) {
            LogMessage log;
            log.from = dongzhuo;
            log.arg = objectName();
            log.type = "#TriggerSkill";
            room->sendLog(log);
            room->notifySkillInvoked(dongzhuo, objectName());

            QString result = room->askForChoice(dongzhuo, "benghuai", "hp+maxhp");
            int index = (result == "hp") ? 1 : 2;
            room->broadcastSkillInvoke(objectName(), index);
            if (result == "hp")
                room->loseHp(dongzhuo);
            else
                room->loseMaxHp(dongzhuo);
        }

        return false;
    }
};

class Baonue: public TriggerSkill {
public:
    Baonue(): TriggerSkill("baonue$") {
        events << Damage << PreDamageDone;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
        if (event == PreDamageDone && damage.from)
            damage.from->tag["InvokeBaonue"] = damage.from->getKingdom() == "qun";
        else if (event == Damage && player->tag.value("InvokeBaonue", false).toBool() && player->isAlive()) {
            QList<ServerPlayer *> dongzhuos;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->hasLordSkill(objectName()))
                    dongzhuos << p;
            }

            while (!dongzhuos.isEmpty()) {
                if (player->askForSkillInvoke(objectName())) {
                    ServerPlayer *dongzhuo = room->askForPlayerChosen(player, dongzhuos, objectName());
                    dongzhuo->setFlags("baonue_used"); //for AI
                    dongzhuos.removeOne(dongzhuo);

                    LogMessage log;
                    log.type = "#InvokeOthersSkill";
                    log.from = player;
                    log.to << dongzhuo;
                    log.arg = objectName();
                    room->sendLog(log);
                    room->notifySkillInvoked(dongzhuo, objectName());

                    JudgeStruct judge;
                    judge.pattern = QRegExp("(.*):(spade):(.*)");
                    judge.good = true;
                    judge.reason = objectName();
                    judge.who = player;

                    room->judge(judge);

                    if (judge.isGood()) {
                        room->broadcastSkillInvoke(objectName());

                        RecoverStruct recover;
                        recover.who = player;
                        room->recover(dongzhuo, recover);
                    }
                } else
                    break;
            }

            foreach (ServerPlayer *dongzhuo, room->getAllPlayers())
                dongzhuo->setFlags("-baonue_used");
        }
        return false;
    }
};

ThicketPackage::ThicketPackage()
    : Package("thicket")
{
    General *xuhuang = new General(this, "xuhuang", "wei");
    xuhuang->addSkill(new Duanliang);
    xuhuang->addSkill(new DuanliangTargetMod);
    related_skills.insertMulti("duanliang", "#duanliang-target");

    General *caopi = new General(this, "caopi$", "wei", 3);
    caopi->addSkill(new Xingshang);
    caopi->addSkill(new Fangzhu);
    caopi->addSkill(new Songwei);
    caopi->addSkill(new SPConvertSkill("caopi", "heg_caopi"));

    General *menghuo = new General(this, "menghuo", "shu");
    menghuo->addSkill(new SavageAssaultAvoid("huoshou"));
    menghuo->addSkill(new Huoshou);
    menghuo->addSkill(new Zaiqi);
    related_skills.insertMulti("huoshou", "#sa_avoid_huoshou");

    General *zhurong = new General(this, "zhurong", "shu", 4, false);
    zhurong->addSkill(new SavageAssaultAvoid("juxiang"));
    zhurong->addSkill(new Juxiang);
    zhurong->addSkill(new Lieren);
    related_skills.insertMulti("juxiang", "#sa_avoid_juxiang");

    General *sunjian = new General(this, "sunjian", "wu");
    sunjian->addSkill(new Yinghun);

    General *lusu = new General(this, "lusu", "wu", 3);
    lusu->addSkill(new Haoshi);
    lusu->addSkill(new HaoshiViewAsSkill);
    lusu->addSkill(new HaoshiGive);
    lusu->addSkill(new Dimeng);
    related_skills.insertMulti("haoshi", "#haoshi");
    related_skills.insertMulti("haoshi", "#haoshi-give");

    General *dongzhuo = new General(this, "dongzhuo$", "qun", 8);
    dongzhuo->addSkill(new Jiuchi);
    dongzhuo->addSkill(new Roulin);
    dongzhuo->addSkill(new Benghuai);
    dongzhuo->addSkill(new Baonue);

    General *jiaxu = new General(this, "jiaxu", "qun", 3);
    jiaxu->addSkill(new Wansha);
    jiaxu->addSkill(new WanshaPrevent);
    related_skills.insertMulti("wansha", "#wansha-prevent");
    jiaxu->addSkill(new MarkAssignSkill("@chaos", 1));
    jiaxu->addSkill(new Luanwu);
    jiaxu->addSkill(new Weimu);
    jiaxu->addSkill(new SPConvertSkill("jiaxu", "sp_jiaxu"));
    related_skills.insertMulti("luanwu", "#@chaos-1");

    addMetaObject<DimengCard>();
    addMetaObject<LuanwuCard>();
    addMetaObject<YinghunCard>();
    addMetaObject<FangzhuCard>();
    addMetaObject<HaoshiCard>();
}

ADD_PACKAGE(Thicket)

