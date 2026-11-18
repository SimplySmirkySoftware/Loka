#ifndef LOKA_TESTS_NATIVE_TEXT_STORE_ADMISSION_HPP
#define LOKA_TESTS_NATIVE_TEXT_STORE_ADMISSION_HPP

#include "app/nodes/boundary/StdComposition.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/TextDocumentAccess.hpp"
#include "support/TextStoreConformance.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Runs both native rails through the same two-window App admission. */
      template <class NativeWindow, class Root> class NativeTextStoreAdmission
      {
        class CountedWindow : public NativeWindow
        {
        public:
          explicit CountedWindow(PlatformContext &context)
              : NativeWindow(&context, WindowProps().visible(true).scene(new scene::Scene(scene::Boundary<Root>()))),
                drains_(0),
                syncs_(0)
          {
          }
          virtual void drainNativeRetirements()
          {
            ++this->drains_;
            NativeWindow::drainNativeRetirements();
          }
          virtual void synchronizeScenePlatform()
          {
            ++this->syncs_;
            NativeWindow::synchronizeScenePlatform();
          }
          struct Counts
          {
            unsigned drains, syncs;
          };
          Counts counts() const
          {
            Counts result = {this->drains_, this->syncs_};
            return result;
          }
          void checkPasses(const Counts &before, unsigned drains) const
          {
            LOKA_VERIFY(this->drains_ == before.drains + drains && this->syncs_ == before.syncs);
          }

        private:
          unsigned drains_, syncs_;
        };

        NullPlatformContext context_;
        CountedWindow first_, second_;
        WindowAdmissionTestApp app_;

        void settle()
        {
          this->app_.flush();
          this->app_.reconcileAtCompletion();
          this->app_.flush();
          LOKA_VERIFY(!this->app_.hasPendingWindowAdmission());
        }
        static TextDocument &document(CountedWindow &window)
        {
          return static_cast<Root *>(dsl::testing::SceneTestAccess::rootBoundary(*window.scene()))->text;
        }

        static scene::TextStoreRow &windowStore(CountedWindow &window)
        {
          const scene::TextDocumentRegistration *registration = TextDocumentAccess::registration(document(window));
          LOKA_VERIFY(registration && registration->store());
          return *registration->store();
        }

      public:
        NativeTextStoreAdmission()
            : first_(this->context_),
              second_(this->context_),
              app_(this->first_, &this->second_)
        {
          this->settle();
        }
        scene::TextStoreRow &store()
        {
          return windowStore(this->first_);
        }
        scene::TextStoreRow &otherStore()
        {
          return windowStore(this->second_);
        }
        scene::TextStoreRow &replace()
        {
          TextDocument &doc = document(this->first_);
          LOKA_VERIFY(doc.replace(TextReplacement::Empty()) == scene::POST_ACCEPTED);
          this->app_.reconcileAtCompletion();
          LOKA_VERIFY(doc.replaceReply()->get().kind() == scene::Reply<TextReplacement>::GRANTED);
          scene::TextStoreRow &fresh = this->store();
          LOKA_VERIFY(TextStoreTestAccess::retiredCount(fresh) == 1);
          return fresh;
        }
        void checkReplacementAdmission(scene::TextStoreRow &fresh)
        {
          const typename CountedWindow::Counts a = this->first_.counts(), b = this->second_.counts();
          const bool pending = this->app_.hasPendingWindowAdmission();
          this->app_.flush();
          LOKA_VERIFY(TextStoreTestAccess::retiredCount(fresh) == 0);
          LOKA_VERIFY(pending && !this->app_.hasPendingWindowAdmission());
          this->first_.checkPasses(a, 1);
          this->second_.checkPasses(b, 0);
          this->app_.flush();
          this->first_.checkPasses(a, 1);
          this->second_.checkPasses(b, 0);
          LOKA_VERIFY(!this->app_.hasPendingWindowAdmission());
        }
        void checkRefusal(TextReplaceResult reason)
        {
          TextDocument &doc = document(this->first_);
          const scene::TextDocumentRegistration *registration = TextDocumentAccess::registration(doc);
          scene::TextStoreRow &old = this->store();
          LOKA_VERIFY(doc.replace(TextReplacement::Empty()) == scene::POST_ACCEPTED);
          this->app_.reconcileAtCompletion();
          LOKA_VERIFY(doc.replaceReply()->get().kind() == scene::Reply<TextReplacement>::REFUSED);
          LOKA_VERIFY(doc.replaceReply()->get().reason() == reason);
          LOKA_VERIFY(TextDocumentAccess::registration(doc) == registration && registration->store() == &old);
          LOKA_VERIFY(TextStoreTestAccess::retiredCount(old) == 1 && this->app_.hasPendingWindowAdmission());
          this->app_.flush();
          LOKA_VERIFY(&this->store() == &old && TextStoreTestAccess::retiredCount(old) == 0);
        }
        void checkBusy()
        {
          scene::TextStoreRow &fresh = this->replace();
          {
            scene::BorrowScope busy(fresh.controller());
            LOKA_VERIFY(this->first_.scene()->isBusy() && !this->app_.hasPendingWindowAdmission());
            this->app_.flush();
            LOKA_VERIFY(TextStoreTestAccess::retiredCount(fresh) == 1);
          }
          LOKA_VERIFY(this->app_.hasPendingWindowAdmission());
          this->app_.flush();
          LOKA_VERIFY(TextStoreTestAccess::retiredCount(fresh) == 0);
        }
        void checkRemount()
        {
          scene::IPlatformController *controller = &this->store().controller();
          scene::Scene *next = new scene::Scene(scene::Boundary<Root>());
          LOKA_VERIFY(this->first_.sceneManager()->requestReplacement(next));
          this->settle();
          LOKA_VERIFY(this->first_.scene() == next && &this->store().controller() == controller);
          this->checkReplacementAdmission(this->replace());
        }
      };
    } // namespace testing
  } // namespace app
} // namespace loka

#endif
