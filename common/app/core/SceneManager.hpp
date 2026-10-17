#ifndef LOKA_SCENEMANAGER_HPP
#define LOKA_SCENEMANAGER_HPP

#include "core/diag/LifecycleAudit.hpp"
#include "app/scene/Scene.hpp"

class Window;

namespace loka
{
  namespace app
  {
    namespace testing
    {
      class SceneManagerTestAccess;
    }

    namespace detail
    {
      /** Owns detached scenes until the enclosing Window flush cycle closes. */
      class SceneRetirePool
      {
      public:
        SceneRetirePool()
            : head_(0),
              draining_(0)
        {
        }

        ~SceneRetirePool()
        {
          while (!this->empty())
          {
            this->drain();
          }
        }

        bool empty() const
        {
          return this->head_ == 0;
        }

        /** Counts this pool's pending retirement rows without cached state. */
        size_t size() const
        {
          size_t count = 0;
          for (const loka::app::scene::Scene *entry = this->head_; entry; entry = entry->retiredNextScene_)
            ++count;
          return count;
        }

        void retire(loka::app::scene::Scene *scene)
        {
          if (!scene || this->contains(scene))
          {
            return;
          }
          scene->retiredNextScene_ = this->head_;
          this->head_ = scene;
        }

        /** Drains one snapshot; nested drains are no-ops and new retirees wait. */
        void drain()
        {
          if (this->draining_ || !this->head_)
          {
            return;
          }

          this->drainSnapshot(this->head_);
        }

        /** Captures the eligible suffix at admission; newer retirements wait. */
        loka::app::scene::Scene *snapshot() const { return this->head_; }

        void drainSnapshot(loka::app::scene::Scene *first)
        {
          if (this->draining_ || !first)
            return;
          loka::app::scene::Scene **link = &this->head_;
          while (*link && *link != first)
            link = &(*link)->retiredNextScene_;
          assert(*link == first);
          if (!*link)
            return;
          *link = 0;
          this->draining_ = first;
          while (this->draining_)
          {
            loka::app::scene::Scene *scene = this->draining_;
            loka::app::scene::Scene *next = scene->retiredNextScene_;
            delete scene;
            this->draining_ = next;
          }
        }

        bool contains(loka::app::scene::Scene *scene) const
        {
          return containsIn(this->head_, scene) || containsIn(this->draining_, scene);
        }

      private:
        static bool containsIn(loka::app::scene::Scene *entry, loka::app::scene::Scene *scene)
        {
          for (; entry; entry = entry->retiredNextScene_)
            if (entry == scene)
              return true;
          return false;
        }

        loka::app::scene::Scene *head_;
        loka::app::scene::Scene *draining_;

        SceneRetirePool(const SceneRetirePool &);
        SceneRetirePool &operator=(const SceneRetirePool &);
      };
    } // namespace detail
  } // namespace app
} // namespace loka

class SceneManager LOKA_AUDITED(SceneManager)
{
public:
  SceneManager();
  ~SceneManager();

  /** Adopts the desired scene for the next Window admission. Returns false
      for a retired identity; retirement cannot be reversed. ignoredFrom is
      retained for source compatibility. Null requests and requests without a
      live Window owner are refused; a refused new target remains caller-owned. */
  bool commitTransaction(loka::app::scene::Scene *ignoredFrom,
                         loka::app::scene::Scene *to);
  /** Records detach for the next App admission; the last request wins.
      Callback callers do O(1) work and walk no composition rows. */
  void requestDetach();
  /** Requests one fresh root generation at the next App admission.
      A refused composition retries at the following admission unless newer
      intent supersedes it. Duplicate requests collapse; callback cost is O(1),
      with no row walk. */
  void requestRearm();
  /** App scheduling query: O(1) per Window, with no row walk. */
  bool hasPendingWork() const
  {
    return this->request_ != REQUEST_NONE || this->hasPendingReplacement();
  }
  /** The admission predicate is derived from the desired and installed identities. */
  bool hasPendingReplacement() const
  {
    return this->desired_ != this->currentScene_;
  }
  bool hasRetiredScenes() const
  {
    return !this->retiredScenes_.empty();
  }

  void setWindow(Window *window)
  {
    window_ = window;
  }
  Window *window() const
  {
    return window_;
  }

private:
  friend class loka::app::testing::SceneManagerTestAccess;
  friend class Window;

  void seedScene(loka::app::scene::Scene *scene);
  /** One admission snapshot. Replacement runs first; its installed result
      receives the captured request. Observer requests wait for next admission. */
  bool applyPendingWork();
  bool applyReplacement();
  /** Installs the identity synchronously inside seat apply, with applying_ set. */
  void installScene(loka::app::scene::Scene *scene);
  /** Publishes retirement synchronously and unmounts the Scene. */
  void retireScene(loka::app::scene::Scene *scene);
  /** Close admission retires the current Scene before Window reclamation. */
  void retireCurrentScene();

  /** Seat-owned last-request-wins value; no Scene pointer can dangle here. */
  enum SceneRequest { REQUEST_NONE, REQUEST_DETACH, REQUEST_REARM };
  void request(SceneRequest value) { this->request_ = value; }
  SceneRequest request_;

  loka::app::scene::Scene *currentScene_;
  loka::app::scene::Scene *desired_;
  loka::app::scene::Scene *applying_;
  loka::app::detail::SceneRetirePool retiredScenes_;
  Window *window_;
#ifdef TEST_BUILD
  /** Last refused candidate, owned as desired or retired until the next admission. */
  loka::app::scene::Scene *lastPrepareRefusal_;
#endif
};

#endif // LOKA_SCENEMANAGER_HPP
