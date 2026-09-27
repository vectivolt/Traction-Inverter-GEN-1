# QtTest suites. The GUI smoke test runs offscreen against the simulator bridge (TT_SIM_BRIDGE); without the bridge it
# skips, and says so.
function(tt_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE ttlib Qt6::Test tt_warnings_tests)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TT_SIM_BRIDGE=${TT_SIM_BRIDGE}")
endfunction()

tt_test(tst_codec)
tt_test(tst_bridgeparser)
tt_test(tst_params)
tt_test(tst_alerts)
tt_test(tst_replay)
tt_test(tst_gui_smoke)
set_tests_properties(tst_gui_smoke PROPERTIES TIMEOUT 180)
# CanTransport against the real firmware: the simulator bridge on Qt's virtual CAN-FD bus (BridgeCanGateway); skips
# without the bridge or the virtualcan plugin
tt_test(tst_can_bridge)
set_tests_properties(tst_can_bridge PROPERTIES TIMEOUT 30)
# FW-38 from the tool against the real firmware: images signed at test time (node firmware/tools/sign-image.mjs), the
# update through BridgeTransport and through CanTransport on the virtual CAN-FD bus; skips without the bridge, Node or
# the virtualcan plugin
tt_test(tst_update)
set_tests_properties(tst_update PROPERTIES TIMEOUT 120)
