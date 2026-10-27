#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "app/style/LineCursor.hpp"
using namespace loka::app;
using namespace loka::app::scene;
void pin(SeatOperation<LineCursor, LineCursor> &op) { RequestSettlement<LineCursor>::settle(op, SETTLE_INPUT, LineCursor()); }
