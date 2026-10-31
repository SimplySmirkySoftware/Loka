# loka_compile_pin(<name> REFUSES <tu> ACCEPTS <tu>)
function(loka_compile_pin NAME)
  cmake_parse_arguments(PIN "" "REFUSES;ACCEPTS" "" ${ARGN})
  if(NOT PIN_REFUSES OR NOT PIN_ACCEPTS OR PIN_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "loka_compile_pin requires REFUSES <tu> ACCEPTS <tu>")
  endif()

  add_library(compilePin_${NAME}_refuses OBJECT EXCLUDE_FROM_ALL ${PIN_REFUSES})
  add_library(compilePin_${NAME}_accepts OBJECT ${PIN_ACCEPTS})
  foreach(kind IN ITEMS refuses accepts)
    set(target compilePin_${NAME}_${kind})
    target_link_libraries(${target} PRIVATE LokaContractTestEnvironment)
    loka_enable_warnings(${target})
    set_target_properties(${target} PROPERTIES
      CXX_STANDARD 98
      CXX_STANDARD_REQUIRED YES
    )
  endforeach()

  if(LOKA_COMPILE_PINS)
    add_test(NAME compileRefuses-${NAME}
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
        --config $<CONFIG> --target compilePin_${NAME}_refuses)
    set_tests_properties(compileRefuses-${NAME} PROPERTIES
      WILL_FAIL TRUE RESOURCE_LOCK loka-build-dir)
  endif()
  add_test(NAME compileAccepts-${NAME}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
      --config $<CONFIG> --target compilePin_${NAME}_accepts)
  set_tests_properties(compileAccepts-${NAME} PROPERTIES
    RESOURCE_LOCK loka-build-dir)
endfunction()

loka_compile_pin(borrowed-state-is-read-only
  REFUSES pins/refuses_set_on_borrowed_state.cpp
  ACCEPTS pins/accepts_get_on_borrowed_state.cpp
)

loka_compile_pin(bindings-need-the-token
  REFUSES pins/refuses_bindings_need_the_token.cpp
  ACCEPTS pins/accepts_bindings_need_the_token.cpp
)

loka_compile_pin(seat-runtime-read-is-a-copy
  REFUSES pins/refuses_seat_runtime_read.cpp
  ACCEPTS pins/accepts_seat_runtime_read.cpp
)

loka_compile_pin(keyed-declarer-is-a-member-of-the-boundary
  REFUSES pins/refuses_keyed_declarer_member.cpp
  ACCEPTS pins/accepts_keyed_declarer_member.cpp
)

loka_compile_pin(keyed-declarer-owner-is-a-boundary
  REFUSES pins/refuses_keyed_declarer_not_a_boundary.cpp
  ACCEPTS pins/accepts_keyed_declarer_member.cpp
)

loka_compile_pin(observablelist-copy
  REFUSES pins/refuses_observablelist_copy.cpp
  ACCEPTS pins/accepts_observablelist_copy.cpp
)

loka_compile_pin(observablelist-assign
  REFUSES pins/refuses_observablelist_assign.cpp
  ACCEPTS pins/accepts_observablelist_assign.cpp
)

loka_compile_pin(mirroredlist-copy
  REFUSES pins/refuses_mirroredlist_copy.cpp
  ACCEPTS pins/accepts_mirroredlist_copy.cpp
)

loka_compile_pin(mirroredlist-assign
  REFUSES pins/refuses_mirroredlist_assign.cpp
  ACCEPTS pins/accepts_mirroredlist_assign.cpp
)

loka_compile_pin(lazyscope-requires-owning-node
  REFUSES pins/refuses_lazyscope_component.cpp
  ACCEPTS pins/accepts_lazyscope_owner.cpp
)

loka_compile_pin(lazyflex-requires-component-props
  REFUSES pins/refuses_lazyflex_component_props.cpp
  ACCEPTS pins/accepts_lazyflex_component_props.cpp
)

loka_compile_pin(tracker-copy
  REFUSES pins/refuses_tracker_copy.cpp
  ACCEPTS pins/accepts_tracker_construction.cpp
)

loka_compile_pin(tracker-assign
  REFUSES pins/refuses_tracker_assign.cpp
  ACCEPTS pins/accepts_tracker_construction.cpp
)

loka_compile_pin(nestable-definition-copy
  REFUSES pins/refuses_nestable_definition_copy.cpp
  ACCEPTS pins/accepts_nestable_definition_construction.cpp
)

loka_compile_pin(nestable-definition-assign
  REFUSES pins/refuses_nestable_definition_assign.cpp
  ACCEPTS pins/accepts_nestable_definition_construction.cpp
)

loka_compile_pin(small-object-pool-source-alignment
  REFUSES pins/refuses_small_object_pool_unaligned_source.cpp
  ACCEPTS pins/accepts_small_object_pool_aligned_source.cpp
)

loka_compile_pin(branch-seat-registration-plan-copy
  REFUSES pins/refuses_branch-seat-registration-plan-copy.cpp
  ACCEPTS pins/accepts_branch-seat-registration-plan-copy.cpp
)

loka_compile_pin(scene-detach-is-seat-owned
  REFUSES pins/refuses_scene_detach.cpp
  ACCEPTS pins/accepts_scene_requests.cpp
)

loka_compile_pin(scene-attach-is-seat-owned
  REFUSES pins/refuses_scene_attach.cpp
  ACCEPTS pins/accepts_scene_requests.cpp
)

loka_compile_pin(scene-unmount-is-seat-owned
  REFUSES pins/refuses_scene_unmount.cpp
  ACCEPTS pins/accepts_scene_requests.cpp
)

loka_compile_pin(scene-lifecycle-is-seat-owned
  REFUSES pins/refuses_scene_lifecycle.cpp
  ACCEPTS pins/accepts_scene_requests.cpp
)

loka_compile_pin(scene-teardown-is-window-owned
  REFUSES pins/refuses_window_scene_teardown.cpp
  ACCEPTS pins/accepts_external_window_scene_teardown.cpp
)

loka_compile_pin(open-file-dialog-props-requires-node-state
  REFUSES pins/refuses_open_file_dialog_props_raw_state.cpp
  ACCEPTS pins/accepts_open_file_dialog_node_state.cpp
)

loka_compile_pin(open-file-dialog-definition-requires-node-state
  REFUSES pins/refuses_open_file_dialog_definition_raw_state.cpp
  ACCEPTS pins/accepts_open_file_dialog_node_state.cpp
)

loka_compile_pin(edittext-requires-write-seat
  REFUSES pins/refuses_edittext_raw_mutable_state.cpp
  ACCEPTS pins/accepts_edittext_write_seat.cpp
)

loka_compile_pin(seat-descriptor-non-node
  REFUSES pins/refuses_seat_non_node.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-descriptor-private-node
  REFUSES pins/refuses_seat_private_node.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-descriptor-ambiguous-node
  REFUSES pins/refuses_seat_ambiguous_node.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-descriptor-incomplete-node
  REFUSES pins/refuses_seat_incomplete_node.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-descriptor-zero
  REFUSES pins/refuses_seat_zero.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-descriptor-negative
  REFUSES pins/refuses_seat_negative.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-descriptor-unterminated
  REFUSES pins/refuses_seat_unterminated.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-descriptor-missing
  REFUSES pins/refuses_seat_missing.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-descriptor-missing-direct
  REFUSES pins/refuses_seat_missing_direct.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-descriptor-wrong-wrapper
  REFUSES pins/refuses_seat_wrong_wrapper.cpp
  ACCEPTS pins/accepts_seat_descriptor.cpp
)

loka_compile_pin(seat-ticket-copy
  REFUSES pins/refuses_seat_ticket_copy.cpp
  ACCEPTS pins/accepts_seat_ticket_borrow.cpp
)

loka_compile_pin(derived-node-state-is-read-only
  REFUSES pins/refuses_derived_node_state_set.cpp
  ACCEPTS pins/accepts_derived_node_state_read.cpp
)

loka_compile_pin(derived-node-needs-mutable-input
  REFUSES pins/refuses_derived_node_derived_dependency.cpp
  ACCEPTS pins/accepts_derived_node_mutable_dependency.cpp
)

loka_compile_pin(style-size-vocabulary-is-closed
  REFUSES pins/refuses_style_size_11.cpp
  ACCEPTS pins/accepts_style_size_12.cpp
)

loka_compile_pin(text-style-only-applies-to-text
  REFUSES pins/refuses_canvas_text_style.cpp
  ACCEPTS pins/accepts_text_text_style.cpp
)

loka_compile_pin(style-families-do-not-cross-merge
  REFUSES pins/refuses_cross_family_style_merge.cpp
  ACCEPTS pins/accepts_same_family_style_merge.cpp
)

loka_compile_pin(attributed-block-style
  REFUSES pins/refuses_attributed_block_style.cpp
  ACCEPTS pins/accepts_attributed_block_style.cpp
)

loka_compile_pin(attributed-string-conversion
  REFUSES pins/refuses_attributed_string_conversion.cpp
  ACCEPTS pins/accepts_attributed_string_conversion.cpp
)

loka_compile_pin(reported-set
  REFUSES pins/refuses_reported_set.cpp
  ACCEPTS pins/accepts_reported_set.cpp
)

loka_compile_pin(editor-fact-seat
  REFUSES pins/refuses_editor_fact_seat.cpp
  ACCEPTS pins/accepts_editor_fact_seat.cpp
)

loka_compile_pin(editor-document-construction
  REFUSES pins/refuses_editor_document_construction.cpp
  ACCEPTS pins/accepts_editor_document_construction.cpp
)

loka_compile_pin(editor-document-access
  REFUSES pins/refuses_editor_document_access.cpp
  ACCEPTS pins/accepts_editor_document_access.cpp
)

loka_compile_pin(editor-old-cursor-constructor
  REFUSES pins/refuses_editor_old_cursor_constructor.cpp
  ACCEPTS pins/accepts_editor_old_cursor_constructor.cpp
)

loka_compile_pin(reported-mutable-alias
  REFUSES pins/refuses_reported_mutable_alias.cpp
  ACCEPTS pins/accepts_reported_mutable_alias.cpp
)

loka_compile_pin(reported-props-door
  REFUSES pins/refuses_reported_props_door.cpp
  ACCEPTS pins/accepts_reported_props_door.cpp
)

loka_compile_pin(request-reply-read-only
  REFUSES pins/refuses_request-reply-read-only.cpp
  ACCEPTS pins/accepts_request-reply-read-only.cpp
)

loka_compile_pin(request-no-node-state
  REFUSES pins/refuses_request-no-node-state.cpp
  ACCEPTS pins/accepts_request-no-node-state.cpp
)

loka_compile_pin(editor-requires-request
  REFUSES pins/refuses_editor-requires-request.cpp
  ACCEPTS pins/accepts_editor-requires-request.cpp
)


loka_compile_pin(editor-seam-key
  REFUSES pins/refuses_editor_seam_key.cpp
  ACCEPTS pins/accepts_editor_seam_key.cpp
)

loka_compile_pin(editor-wrong-seam-key
  REFUSES pins/refuses_editor_wrong_seam_key.cpp
  ACCEPTS pins/accepts_editor_wrong_seam_key.cpp
)

loka_compile_pin(editor-context-friend
  REFUSES pins/refuses_editor_context_friend.cpp
  ACCEPTS pins/accepts_editor_context_friend.cpp
)

loka_compile_pin(handler-wrong-seam-key
  REFUSES pins/refuses_handler_wrong_seam_key.cpp
  ACCEPTS pins/accepts_handler_wrong_seam_key.cpp
)

loka_compile_pin(request-wall-direct-command
  REFUSES pins/refuses_request-wall-direct-command.cpp
  ACCEPTS pins/accepts_request-wall-direct-command.cpp
)

loka_compile_pin(request-wall-direct-command-reply
  REFUSES pins/refuses_request-wall-direct-command-reply.cpp
  ACCEPTS pins/accepts_request-wall-direct-command-reply.cpp
)

loka_compile_pin(request-wall-direct-untyped
  REFUSES pins/refuses_request-wall-direct-untyped.cpp
  ACCEPTS pins/accepts_request-wall-direct-untyped.cpp
)

loka_compile_pin(request-wall-direct-untyped-reply
  REFUSES pins/refuses_request-wall-direct-untyped-reply.cpp
  ACCEPTS pins/accepts_request-wall-direct-untyped-reply.cpp
)

loka_compile_pin(request-wall-batch-command
  REFUSES pins/refuses_request-wall-batch-command.cpp
  ACCEPTS pins/accepts_request-wall-batch-command.cpp
)

loka_compile_pin(request-wall-batch-command-reply
  REFUSES pins/refuses_request-wall-batch-command-reply.cpp
  ACCEPTS pins/accepts_request-wall-batch-command-reply.cpp
)

loka_compile_pin(request-wall-batch-untyped
  REFUSES pins/refuses_request-wall-batch-untyped.cpp
  ACCEPTS pins/accepts_request-wall-batch-untyped.cpp
)

loka_compile_pin(request-wall-batch-untyped-reply
  REFUSES pins/refuses_request-wall-batch-untyped-reply.cpp
  ACCEPTS pins/accepts_request-wall-batch-untyped-reply.cpp
)

loka_compile_pin(request-wall-immediate-command
  REFUSES pins/refuses_request-wall-immediate-command.cpp
  ACCEPTS pins/accepts_request-wall-immediate-command.cpp
)

loka_compile_pin(request-wall-immediate-command-reply
  REFUSES pins/refuses_request-wall-immediate-command-reply.cpp
  ACCEPTS pins/accepts_request-wall-immediate-command-reply.cpp
)

loka_compile_pin(request-wall-immediate-untyped
  REFUSES pins/refuses_request-wall-immediate-untyped.cpp
  ACCEPTS pins/accepts_request-wall-immediate-untyped.cpp
)

loka_compile_pin(request-wall-immediate-untyped-reply
  REFUSES pins/refuses_request-wall-immediate-untyped-reply.cpp
  ACCEPTS pins/accepts_request-wall-immediate-untyped-reply.cpp
)

loka_compile_pin(request-binding-sourceless-command
  REFUSES pins/refuses_request-binding-sourceless-command.cpp
  ACCEPTS pins/accepts_request-binding-sourceless-command.cpp
)

loka_compile_pin(request-wall-direct-editor-command
  REFUSES pins/refuses_request-wall-direct-editor-command.cpp
  ACCEPTS pins/accepts_request-wall-direct-editor-command.cpp
)

loka_compile_pin(request-wall-batch-editor-command
  REFUSES pins/refuses_request-wall-batch-editor-command.cpp
  ACCEPTS pins/accepts_request-wall-batch-editor-command.cpp
)

loka_compile_pin(request-wall-immediate-editor-command
  REFUSES pins/refuses_request-wall-immediate-editor-command.cpp
  ACCEPTS pins/accepts_request-wall-immediate-editor-command.cpp
)

loka_compile_pin(request-binding-sourceless-editor-command
  REFUSES pins/refuses_request-binding-sourceless-editor-command.cpp
  ACCEPTS pins/accepts_request-binding-sourceless-editor-command.cpp
)

loka_compile_pin(focus-string
  REFUSES pins/refuses_focus-string.cpp
  ACCEPTS pins/accepts_focus-string.cpp
)

loka_compile_pin(focus-unregistered
  REFUSES pins/refuses_focus-unregistered.cpp
  ACCEPTS pins/accepts_focus-unregistered.cpp
)

loka_compile_pin(focus-window-entry
  REFUSES pins/refuses_focus-window-entry.cpp
  ACCEPTS pins/accepts_focus-window-entry.cpp
)

loka_compile_pin(defer-needs-the-key
  REFUSES pins/refuses_defer_without_key.cpp
  ACCEPTS pins/accepts_defer_testing_door.cpp
)

loka_compile_pin(defer-key-is-private
  REFUSES pins/refuses_defer_key_construction.cpp
  ACCEPTS pins/accepts_defer_testing_door.cpp
)

loka_compile_pin(text-align-enum
  REFUSES pins/refuses_text_align.cpp
  ACCEPTS pins/accepts_text_align.cpp
)

loka_compile_pin(flowslot-needs-owner
  REFUSES pins/refuses_flowslot_default.cpp
  ACCEPTS pins/accepts_flowslot_owner.cpp
)

loka_compile_pin(flowslot-testing-key-is-private
  REFUSES pins/refuses_flowslot_key.cpp
  ACCEPTS pins/accepts_flowslot_testing.cpp
)

loka_compile_pin(focus-post-unregistered
  REFUSES pins/refuses_focus-post.cpp
  ACCEPTS pins/accepts_focus-post.cpp
)

loka_compile_pin(observed-use-copy
  REFUSES pins/refuses_observed_use_copy.cpp
  ACCEPTS pins/accepts_observed_use_borrow.cpp
)

loka_compile_pin(borrow-scope-copy
  REFUSES pins/refuses_borrow-scope-copy.cpp
  ACCEPTS pins/accepts_borrow-scope-copy.cpp
)

loka_compile_pin(borrow-phase-copy
  REFUSES pins/refuses_borrow-phase-copy.cpp
  ACCEPTS pins/accepts_borrow-phase-copy.cpp
)

loka_compile_pin(borrow-controller-copy
  REFUSES pins/refuses_borrow-controller-copy.cpp
  ACCEPTS pins/accepts_borrow-controller-copy.cpp
)

loka_compile_pin(settle-single-owner
  REFUSES pins/refuses_settle-single-owner.cpp
  ACCEPTS pins/accepts_settle-single-owner.cpp
)

loka_compile_pin(settle-multi-owner
  REFUSES pins/refuses_settle-multi-owner.cpp
  ACCEPTS pins/accepts_settle-multi-owner.cpp
)

loka_compile_pin(borrow-owner-construction
  REFUSES pins/refuses_borrow-owner-construction.cpp
  ACCEPTS pins/accepts_borrow-owner-construction.cpp
)

loka_compile_pin(borrow-owner-default
  REFUSES pins/refuses_borrow-owner-default.cpp
  ACCEPTS pins/accepts_borrow-owner-default.cpp
)

loka_compile_pin(borrow-owner-copy
  REFUSES pins/refuses_borrow-owner-copy.cpp
  ACCEPTS pins/accepts_borrow-owner-copy.cpp
)

loka_compile_pin(null-input-body
  REFUSES pins/refuses_null_input_body.cpp
  ACCEPTS pins/accepts_null_input_body.cpp
)

loka_compile_pin(toolbox-input-body
  REFUSES pins/refuses_toolbox_input_body.cpp
  ACCEPTS pins/accepts_toolbox_input_body.cpp
)
foreach(kind IN ITEMS refuses accepts)
  target_include_directories(compilePin_toolbox-input-body_${kind} PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/toolbox/host ${PROJECT_SOURCE_DIR}/tests
    ${PROJECT_SOURCE_DIR}/apple/toolbox/src)
  target_compile_options(compilePin_toolbox-input-body_${kind} PRIVATE
    -include ${PROJECT_SOURCE_DIR}/tests/toolbox/host/ToolboxHost.hpp)
endforeach()

loka_compile_pin(boundary-declaration-is-kernel-owned
  REFUSES pins/refuses_boundary_declaration.cpp
  ACCEPTS pins/accepts_boundary_declaration.cpp
)

loka_compile_pin(paint-source-requires-merge-declaration
  REFUSES pins/refuses_paint_source_declaration.cpp
  ACCEPTS pins/accepts_paint_source_declaration.cpp
)

loka_compile_pin(lazyview-column-requires-recipe
  REFUSES pins/refuses_lazyview_column_recipe.cpp
  ACCEPTS pins/accepts_lazyview_column_recipe.cpp
)

loka_compile_pin(lazyview-row-requires-recipe
  REFUSES pins/refuses_lazyview_row_recipe.cpp
  ACCEPTS pins/accepts_lazyview_row_recipe.cpp
)

loka_compile_pin(lazyview-view-requires-recipe
  REFUSES pins/refuses_lazyview_view_recipe.cpp
  ACCEPTS pins/accepts_lazyview_view_recipe.cpp
)

loka_compile_pin(lazyview-props-requires-recipe
  REFUSES pins/refuses_lazyview_props_recipe.cpp
  ACCEPTS pins/accepts_lazyview_props_recipe.cpp
)

loka_compile_pin(save_file_dialog_result
  REFUSES pins/refuses_save_file_dialog_result.cpp
  ACCEPTS pins/accepts_save_file_dialog_result.cpp)

loka_compile_pin(window_props_menu_bar_retired
  REFUSES pins/refuses_window_props_menu_bar.cpp
  ACCEPTS pins/accepts_window_props_title.cpp
)

loka_compile_pin(spacer-refuses-children
  REFUSES pins/refuses_spacer-refuses-children.cpp
  ACCEPTS pins/accepts_spacer-refuses-children.cpp)

loka_compile_pin(ribbon-control-accepts-only-items
  REFUSES pins/refuses_ribbon-control-accepts-only-items.cpp
  ACCEPTS pins/accepts_ribbon-control-accepts-only-items.cpp
)

loka_compile_pin(ribbon-control-refuses-card
  REFUSES pins/refuses_ribbon-control-card.cpp
  ACCEPTS pins/accepts_ribbon-control-card.cpp
)

loka_compile_pin(ribbon-item-is-not-a-node
  REFUSES pins/refuses_ribbon-item-is-not-a-node.cpp
  ACCEPTS pins/accepts_ribbon-item-is-not-a-node.cpp
)

loka_compile_pin(command-set-slot
  REFUSES pins/refuses_command_set_slot.cpp
  ACCEPTS pins/accepts_command_set_slot.cpp
)

loka_compile_pin(rect-surface-has-no-clear-background
  REFUSES pins/refuses_rect_surface_clear_background.cpp
  ACCEPTS pins/accepts_rect_surface_ground.cpp
)

loka_compile_pin(document_window_seat_requires_facts
  REFUSES pins/refuses_document_window_seat_facts.cpp
  ACCEPTS pins/accepts_document_window_seat_facts.cpp
)

loka_compile_pin(document_window_seat_type_requires_facts
  REFUSES pins/refuses_document_window_seat_type.cpp
  ACCEPTS pins/accepts_document_window_seat_type.cpp
)

loka_compile_pin(dialog_result_cross_payload
  REFUSES pins/refuses_dialog_result_cross_payload.cpp
  ACCEPTS pins/accepts_dialog_result_cross_payload.cpp
)

loka_compile_pin(document_roster_ask_close_private
  REFUSES pins/refuses_document_roster_ask_close.cpp
  ACCEPTS pins/accepts_document_roster_ask_close.cpp
)

loka_compile_pin(show-pointer
  REFUSES pins/refuses_show_pointer.cpp
  ACCEPTS pins/accepts_show_pointer.cpp
)

loka_compile_pin(show-bool
  REFUSES pins/refuses_show_bool.cpp
  ACCEPTS pins/accepts_show_bool.cpp
)

loka_compile_pin(show-temporary
  REFUSES pins/refuses_show_temporary.cpp
  ACCEPTS pins/accepts_show_temporary.cpp
)

loka_compile_pin(show-shift
  REFUSES pins/refuses_show_shift.cpp
  ACCEPTS pins/accepts_show_shift.cpp
)

loka_compile_pin(show-shift-temporary
  REFUSES pins/refuses_show_shift_temporary.cpp
  ACCEPTS pins/accepts_show_shift_temporary.cpp
)

loka_compile_pin(show-conversion
  REFUSES pins/refuses_show_conversion.cpp
  ACCEPTS pins/accepts_show_conversion.cpp
)

loka_compile_pin(show-include-order
  REFUSES pins/refuses_show_include_order.cpp
  ACCEPTS pins/accepts_show_include_order.cpp
)

loka_compile_pin(show-derived
  REFUSES pins/refuses_show_derived.cpp
  ACCEPTS pins/accepts_show_derived.cpp
)

# The host twin exposes the same update/flush boundary without a Classic SDK.
loka_compile_pin(toolbox-update-entry
  REFUSES pins/refuses_toolbox_update_entry.cpp
  ACCEPTS pins/accepts_toolbox_update_entry.cpp
)
foreach(kind IN ITEMS refuses accepts)
  target_include_directories(compilePin_toolbox-update-entry_${kind} PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/toolbox/host ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/apple/toolbox/src)
  target_compile_definitions(compilePin_toolbox-update-entry_${kind} PRIVATE LOKA_HOST_ZSTACK_UNITS=1)
endforeach()

loka_compile_pin(request-header-neutral
  REFUSES pins/refuses_request-header-neutral.cpp
  ACCEPTS pins/accepts_request-header-neutral.cpp
)

loka_compile_pin(settle-request-reason
  REFUSES pins/refuses_settle-request-reason.cpp
  ACCEPTS pins/accepts_settle-request-reason.cpp
)

loka_compile_pin(request-reasons-distinct
  REFUSES pins/refuses_request-reasons-distinct.cpp
  ACCEPTS pins/accepts_request-reasons-distinct.cpp
)

# Focus deliberately has no Reason; the editor return type is unchanged.
# No meaningful refusing twin exists for this compatibility acceptance pin.
add_library(compilePin_request-focus-and-editor_accepts OBJECT pins/accepts_request-focus-and-editor.cpp)
target_link_libraries(compilePin_request-focus-and-editor_accepts PRIVATE LokaContractTestEnvironment)
loka_enable_warnings(compilePin_request-focus-and-editor_accepts)
set_target_properties(compilePin_request-focus-and-editor_accepts PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
add_test(NAME compileAccepts-request-focus-and-editor
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
    --config $<CONFIG> --target compilePin_request-focus-and-editor_accepts)
set_tests_properties(compileAccepts-request-focus-and-editor PROPERTIES RESOURCE_LOCK loka-build-dir)

loka_compile_pin(scrollview-visit-arity
  REFUSES pins/refuses_scrollview_visit_arity.cpp
  ACCEPTS pins/accepts_scrollview_visit_arity.cpp
)

loka_compile_pin(toolbar-accepts-only-items
  REFUSES pins/refuses_toolbar-accepts-only-items.cpp
  ACCEPTS pins/accepts_toolbar-accepts-only-items.cpp)

loka_compile_pin(toolbar-refuses-card
  REFUSES pins/refuses_toolbar-refuses-card.cpp
  ACCEPTS pins/accepts_toolbar-refuses-card.cpp)

loka_compile_pin(toolbar-refuses-text
  REFUSES pins/refuses_toolbar-refuses-text.cpp
  ACCEPTS pins/accepts_toolbar-refuses-text.cpp)

loka_compile_pin(toolbar-item-is-not-a-node
  REFUSES pins/refuses_toolbar-item-is-not-a-node.cpp
  ACCEPTS pins/accepts_toolbar-item-is-not-a-node.cpp)

loka_compile_pin(toolbar-space-is-not-a-node
  REFUSES pins/refuses_toolbar-space-is-not-a-node.cpp
  ACCEPTS pins/accepts_toolbar-space-is-not-a-node.cpp)

loka_compile_pin(toolbar-label-is-not-a-node
  REFUSES pins/refuses_toolbar-label-is-not-a-node.cpp
  ACCEPTS pins/accepts_toolbar-label-is-not-a-node.cpp)

loka_compile_pin(toolbar-space-has-no-onclick
  REFUSES pins/refuses_toolbar-space-has-no-onclick.cpp
  ACCEPTS pins/accepts_toolbar-space-has-no-onclick.cpp)

loka_compile_pin(toolbar-label-refuses-block-style
  REFUSES pins/refuses_toolbar-label-refuses-block-style.cpp
  ACCEPTS pins/accepts_toolbar-label-refuses-block-style.cpp)

loka_compile_pin(toolbar-label-refuses-block-style-after-title
  REFUSES pins/refuses_toolbar-label-refuses-block-style-after-title.cpp
  ACCEPTS pins/accepts_toolbar-label-refuses-block-style-after-title.cpp)

loka_compile_pin(toolbar-item-refuses-live-title
  REFUSES pins/refuses_toolbar-item-refuses-live-title.cpp
  ACCEPTS pins/accepts_toolbar-item-refuses-live-title.cpp)

loka_compile_pin(toolbar-item-refuses-shown-bool
  REFUSES pins/refuses_toolbar-item-refuses-shown-bool.cpp
  ACCEPTS pins/accepts_toolbar-item-refuses-shown-bool.cpp)
