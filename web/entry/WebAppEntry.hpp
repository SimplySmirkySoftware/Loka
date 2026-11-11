#ifndef LOKA_WEB_APP_ENTRY_HPP
#define LOKA_WEB_APP_ENTRY_HPP
class AppConfigurable;
class PlatformContext;
namespace loka
{
  namespace web
  {
    /** Called at most once per module, after admission succeeds. The session
        owns the returned config and deletes it at shutdown. Null means start
        FAILED; a missing definition is an undefined-symbol link error.
        Cost: one config construction, no rail row scans. */
    AppConfigurable *CreateAppConfig(PlatformContext *platform);
  } // namespace web
} // namespace loka
#endif
