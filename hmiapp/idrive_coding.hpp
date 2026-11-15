// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// What the service needs of the car's coding, read inside iDrive and written for it: which key each controller
// button is (platform/controller_layout.hpp) and the brand (platform/car_brand.hpp).
//
// iDrive keeps its coding behind a variant server: hmiplugin::get<IHmiVariantServer>(), then isVariantSet(id)
// (vtable slot 3) and getRole() (slot 21), as CHmiCCSController::pushbuttonToKeycode does in HmiMain3D. The app runs
// inside HmiMain3D, so it asks the same server; both functions are found with dlsym, nothing is linked. If one is
// missing or the server is not there yet, nothing is written: the service keeps its G-style keys and a BMW.
// Runs on the app thread (onInit).
#ifndef ENTRY_AAD_HMIAPP_IDRIVE_CODING_HPP
#define ENTRY_AAD_HMIAPP_IDRIVE_CODING_HPP

namespace evo {
namespace app {

// Reads the coding and writes contract::kControllerFile and contract::kBrandFile. False when either was not written
// (logged); a file not written is removed.
bool writeIdriveCoding();

}  // namespace app
}  // namespace evo

#endif
