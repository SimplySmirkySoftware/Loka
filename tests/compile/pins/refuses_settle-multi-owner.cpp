#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "app/style/LineCursor.hpp"
using namespace loka::app;
using namespace loka::app::scene;
void pin(SettleOwner<LineCursor> &owner, SeatOperation<LineCursor, LineCursor> &a, SeatOperation<LineCursor, LineCursor> &b) { (void)owner; RequestSettlement<LineCursor>::settle(a, b, SETTLE_INPUT, LineCursor()); }
