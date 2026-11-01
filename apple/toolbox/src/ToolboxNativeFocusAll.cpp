/** App-owned component walk: once before service and once before each modal. */
bool ToolboxApp::syncNativeFocusAll(NativeFocusMode mode)
{
  bool active = false;
  if (!this->group_)
    return false;
  const AppComponentGroup::Components components = this->group_->getComponents();
  for (std::size_t i = 0; i < components.size(); ++i)
  {
    Window *window = components[i]->asWindow();
    ToolboxWindow *toolbox = window ? window->asToolboxWindow() : 0;
    ToolboxScenePlatformController *controller = toolbox ? toolbox->scenePlatformController() : 0;
    if (controller && ToolboxInputDoor::syncNativeFocus(*controller, mode))
      active = true;
  }
  return active;
}
