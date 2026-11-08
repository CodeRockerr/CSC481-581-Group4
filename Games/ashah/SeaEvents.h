#pragma once
#include "SeaGameplay.h"
#include "Engine.h"
#include "DeathZone.h"
#include "EventObjects.h"
#include "GamePlayEvents.h"
#include <cmath>
#include <functional>
#include <sstream>

namespace Sea
{
    constexpr EventType SonarEvent = EventTypes::FirstGame + 20;

    struct EventCounts
    {
        int input = 0, collision = 0, death = 0, spawn = 0, sonar = 0, remoteSonar = 0;
    };

    struct SonarState
    {
        float x = 0, y = 0, remaining = 0, cooldown = 0;
        uint32_t owner = 0;
    };

    struct EventState
    {
        EventCounts counts;
        SonarState sonar;
        GameplayEvents::TransientState transient;
        std::vector<std::string> history;
    };

    class Events
    {
    public:
        using Say = std::function<void(const std::string &, float)>;

        Events(Engine &engine, GameObject &player, Progress &progress, PlayerMotion &motion,
               const std::vector<uint32_t> &pearls, Say say, std::function<void()> spawned,
               bool networked = false)
            : engine(engine), player(player), progress(progress), motion(motion), pearls(pearls),
              say(std::move(say)), gameplay(engine.getEvents(), engine.getObjects(), engine.getCamera()),
              networked(networked)
        {
            GameplayEvents::Rules rules;
            rules.onDeath = [this](GameObject &victim, const Event &event) -> std::optional<std::string>
            {
                if (&victim != &this->player || this->progress.finished()) return std::nullopt;
                const auto *cause = EventObjects::resolve(this->engine.getObjects(), event.source);
                const bool abyss = cause && cause->has<DeathZone>();
                if (!abyss && this->progress.protectedNow()) return std::nullopt;
                if (!this->progress.loseLife()) return std::nullopt;
                remember("Life lost (" + std::to_string(this->progress.health) + " left) - Death");
                auto *body = this->player.get<PhysicsBody>();
                body->velocityX = body->velocityY = 0;
                this->say(this->progress.gameOver ? "Dive ended. Press R to restart." :
                          abyss ? "The abyss cost one life. Returning to your checkpoint." :
                                  "Hit! Recovered pearls stay safe.", 4);
                if (this->progress.gameOver) return std::nullopt;
                return Spawns[this->progress.checkpoint].name;
            };
            rules.onSpawned = [this, spawned](GameObject &subject, const Event &event)
            {
                if (&subject != &this->player) return;
                this->motion = {};
                this->progress.graceTime = 1.8f;
                const auto *spawn = std::get_if<SpawnEvent>(&event.payload);
                remember("Placed at " + (spawn ? spawn->spawnPoint : "checkpoint") + " - Spawn");
                spawned();
            };
            rules.onCollision = [this](GameObject &subject, GameObject &other, const Event &)
            {
                if (&subject == &this->player) contact(other);
            };
            gameplay.setRules(std::move(rules));
            auto &events = engine.getEvents();
            subscriptions.push_back(events.subscribe(EventTypes::Input, [this](const Event &event)
            {
                const auto *input = std::get_if<InputEvent>(&event.payload);
                if (!input || input->value == 0 || !(event.target == EventObjects::of(this->player))) return;
                if (input->action == "start" && onStart) onStart();
                if (input->action == "restart" && onRestart) onRestart();
                if (input->action == "sonar") emitSonar();
            }));
            subscriptions.push_back(events.subscribe(SonarEvent, [this](const Event &event) { receiveSonar(event); }));
            subscriptions.push_back(events.subscribeAll([this](const Event &event)
            {
                if (event.type == EventTypes::Input) ++counts.input;
                if (event.type == EventTypes::Collision) ++counts.collision;
                if (event.type == EventTypes::Death) ++counts.death;
                if (event.type == EventTypes::Spawn) ++counts.spawn;
            }));
        }

        ~Events()
        {
            for (auto subscription : subscriptions) engine.getEvents().unsubscribe(subscription);
        }

        void input(const std::string &action, float value = 1)
        {
            Event event;
            event.type = EventTypes::Input;
            event.priority = EventPriority::High;
            event.target = EventObjects::of(player);
            event.payload = InputEvent{action, value};
            engine.getEvents().raise(std::move(event));
        }

        void respawn()
        {
            gameplay.raiseSpawn(player, Spawns[progress.checkpoint].name);
        }

        void collisions()
        {
            if (progress.finished()) return;
            auto touching = gameplay.captureTransientState().touching;
            gameplay.produceCollisions(player.getId());
            if (progress.protectedNow()) return;
            engine.getObjects().forEach<Hazard>([&](GameObject &other, Hazard &)
            {
                if (!touching.count(other.getId()) || !touchesHazard(player, other)) return;
                Event event;
                event.type = EventTypes::Collision;
                event.source = EventObjects::of(player);
                event.target = EventObjects::of(other);
                event.payload = CollisionEvent{0, 0, true};
                engine.getEvents().raise(std::move(event));
            });
        }

        void tick(float dt)
        {
            sonar.remaining = std::max(0.f, sonar.remaining - dt);
            sonar.cooldown = std::max(0.f, sonar.cooldown - dt);
        }

        EventState capture() const { return {counts, sonar, gameplay.captureTransientState(), history}; }
        void restore(const EventState &state)
        {
            counts = state.counts;
            sonar = state.sonar;
            gameplay.restoreTransientState(state.transient);
            history = state.history;
        }
        void freshStart()
        {
            sonar = {};
            history.clear();
            gameplay.resetTransientState();
        }

        std::string status() const
        {
            const auto &replay = engine.getReplay();
            std::ostringstream out;
            out << (replay.isPlaying() ? "PLAYBACK" : replay.isRecording() ? "RECORDING" : "LIVE")
                << "  INPUT " << counts.input << "  COLLISION " << counts.collision
                << "  DEATH " << counts.death << "  SPAWN " << counts.spawn
                << "  SONAR RX " << counts.remoteSonar << "  | E SONAR  F5 RECORD  F6 STOP  F7 PLAY  H HISTORY";
            return out.str();
        }

        std::function<void()> onStart, onRestart;
        EventCounts counts;
        SonarState sonar;
        bool historyVisible = false;
        void toggleHistory() { historyVisible = !historyVisible; }
        const std::vector<std::string> &recentEvents() const { return history; }

    private:
        void remember(std::string entry)
        {
            if (history.size() == 6) history.erase(history.begin());
            history.push_back(std::move(entry));
        }

        void contact(GameObject &other)
        {
            if (progress.finished()) return;
            if (auto *pearl = other.get<Pearl>(); pearl && !pearl->collected)
            {
                auto found = std::find(pearls.begin(), pearls.end(), other.getId());
                if (found == pearls.end()) return;
                progress.pearls[static_cast<std::size_t>(found - pearls.begin())] = true;
                pearl->collected = true;
                other.active = false;
                remember("Pearl collected - Collision");
                say(progress.count() == PearlCount ? "ALL PEARLS SECURED! Reach the seabed, then return to the buoy." :
                                                    "Pearl secured. Keep diving!", 4);
            }
            if (auto *power = other.get<PowerUp>(); power && !power->collected)
            {
                power->collected = true;
                other.active = false;
                progress.activatePower();
                remember("Overdrive collected - Collision");
                say("3 SECOND OVERDRIVE! Fish and urchins cannot hurt you.", 3);
            }
            if (other.has<Hazard>() && touchesHazard(player, other) && !progress.protectedNow())
            {
                remember(other.has<Urchin>() ? "Urchin contact - Collision" : "Fish contact - Collision");
                gameplay.raiseDeath(player, &other, "hazard");
            }
        }

        void emitSonar()
        {
            if (progress.finished() || sonar.cooldown > 0) return;
            sonar.cooldown = 4;
            const auto &t = *player.get<Transform>();
            Event event;
            event.type = SonarEvent;
            event.source = EventObjects::of(player);
            event.payload = EventFields{{"x", double(t.x + t.width * .5f)},
                                        {"y", double(t.y + t.height * .5f)}};
            event.shareable = networked;
            engine.getEvents().raise(std::move(event));
        }

        void receiveSonar(const Event &event)
        {
            if (progress.finished()) return;
            const auto *fields = std::get_if<EventFields>(&event.payload);
            if (!fields || !fields->count("x") || !fields->count("y")) return;
            const auto *x = std::get_if<double>(&fields->at("x"));
            const auto *y = std::get_if<double>(&fields->at("y"));
            if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y) ||
                *x < 0 || *x > WorldWidth || *y < 0 || *y > WorldHeight) return;
            sonar.x = static_cast<float>(*x);
            sonar.y = static_cast<float>(*y);
            sonar.owner = event.source.ownerId;
            sonar.remaining = 1.2f;
            ++counts.sonar;
            if (event.origin == EventOrigin::Network)
            {
                ++counts.remoteSonar;
                remember("Teammate sonar received - Network");
                const auto &t = *player.get<Transform>();
                const float dx = t.x + t.width * .5f - sonar.x;
                const float dy = t.y + t.height * .5f - sonar.y;
                if (!progress.finished() && dx * dx + dy * dy <= 380 * 380)
                {
                    progress.graceTime = std::max(progress.graceTime, 1.2f);
                    say("SONAR ASSIST! A nearby captain protected you for 1.2 seconds.", 3);
                }
                else say("A captain sent a sonar pulse. Nearby pearls are highlighted.", 3);
            }
            else
            {
                remember("Sonar activated - Sonar");
                say("SONAR PULSE! Nearby pearls light up. Nearby network teammates receive protection.", 3);
            }
        }

        Engine &engine;
        GameObject &player;
        Progress &progress;
        PlayerMotion &motion;
        const std::vector<uint32_t> &pearls;
        Say say;
        GameplayEvents gameplay;
        bool networked;
        std::vector<std::string> history;
        std::vector<EventManager::Subscription> subscriptions;
    };
}
