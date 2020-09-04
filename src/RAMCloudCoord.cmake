add_library(ramcloudcoord SHARED
  ClientLeaseAuthority.cc
  CoordinatorClusterClock.cc
  CoordinatorServerList.cc
  CoordinatorService.cc
  CoordinatorUpdateManager.cc
  MasterRecoveryManager.cc
  MockExternalStorage.cc
  Tablet.cc
  TableManager.cc
  Recovery.cc
  RuntimeOptions.cc)

link_directories(${CMAKE_CURRENT_SOURCE_DIR})

list(APPEND LIBS ramcloud ramcloudcoord ramcloudserver message)

add_executable(coordinator CoordinatorMain.cc)
target_link_libraries(coordinator PUBLIC ${ramcloud}
                      ${LIBS})