// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "idrive_coding.hpp"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <string>

#include "../platform/car_brand.hpp"
#include "../platform/controller_layout.hpp"
#include "../platform/service_contract.hpp"
#include "app_log.hpp"

namespace evo {
namespace app {

namespace {

// HmiMain3D's exported template instances (nm -D: weak, defined). The usage object is one pointer-sized value,
// returned in r0 (read in pushbuttonToKeycode: the result of get() is stored, then its address is passed to get()).
const char kGetUsage[] = "_ZN9hmiplugin3getI17IHmiVariantServerEENS_25CPluginInstanceCacheUsageIT_EEv";
const char kUsageGet[] = "_ZNK9hmiplugin25CPluginInstanceCacheUsageI17IHmiVariantServerE3getEv";
const unsigned kIsVariantSetSlot = 3;  // CHmiVariantServer vtable (libhmicoding.so): isVariantSet(unsigned)
const unsigned kGetRoleSlot = 21;      // getRole()

typedef uintptr_t (*GetUsageFn)();
typedef void* (*UsageGetFn)(const uintptr_t* usage);
typedef bool (*IsVariantSetFn)(void* server, unsigned id);
typedef int (*GetRoleFn)(void* server);

// iDrive's variant server and the two calls asked of it.
class VariantServer {
public:
    VariantServer() : self_(nullptr), isSet_(nullptr), role_(nullptr) {}
    // False when HmiMain3D does not give it (logged).
    bool find() {
        GetUsageFn getUsage = reinterpret_cast<GetUsageFn>(dlsym(RTLD_DEFAULT, kGetUsage));
        UsageGetFn usageGet = reinterpret_cast<UsageGetFn>(dlsym(RTLD_DEFAULT, kUsageGet));
        if (getUsage && usageGet) {
            const uintptr_t usage = getUsage();
            self_ = usageGet(&usage);
        }
        void** vtable = self_ ? *reinterpret_cast<void***>(self_) : nullptr;
        if (!vtable || !vtable[kIsVariantSetSlot] || !vtable[kGetRoleSlot]) {
            alog("iDrive coding: variant server not found (%s %s): the service keeps G-style keys and a BMW",
                 getUsage ? "get" : "no get", usageGet ? "usage" : "no usage");
            return false;
        }
        isSet_ = reinterpret_cast<IsVariantSetFn>(vtable[kIsVariantSetSlot]);
        role_ = reinterpret_cast<GetRoleFn>(vtable[kGetRoleSlot]);
        return true;
    }
    bool isSet(unsigned id) const { return isSet_(self_, id); }
    int role() const { return role_(self_); }

private:
    void* self_;
    IsVariantSetFn isSet_;
    GetRoleFn role_;
};

// One line to path, through a temporary file renamed over it: the service never reads half a line. On failure the
// file is removed, so the service falls back to its default rather than an old value.
bool writeLine(const char* path, const char* line) {
    const std::string tmp = std::string(path) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    bool written = f && fprintf(f, "%s\n", line) > 0;
    if (f && fclose(f) != 0) {
        written = false;
    }
    if (!written || rename(tmp.c_str(), path) != 0) {
        unlink(tmp.c_str());
        unlink(path);
        return false;
    }
    return true;
}

bool writeControllerCoding(const VariantServer& server) {
    aa::ControllerCoding coding = aa::defaultControllerCoding();
    coding.known = true;
    coding.gStyle = server.isSet(aa::kVariantGStyle);
    coding.v5141 = server.isSet(aa::kVariant5141);
    coding.v4e59 = server.isSet(aa::kVariant4E59);
    coding.v4f1e = server.isSet(aa::kVariant4F1E);
    coding.v5486 = server.isSet(aa::kVariant5486);
    coding.role = server.role();
    char line[96];
    aa::formatControllerCoding(coding, line, sizeof(line));
    const bool written = writeLine(contract::kControllerFile, line);
    alog("controller coding: %s (%s controller)%s", line, coding.gStyle ? "G-style" : "F-style",
         written ? "" : ", not written");
    return written;
}

bool writeBrand(const VariantServer& server) {
    const aa::CarBrand brand = server.isSet(aa::kVariantBrandMini) ? aa::CarBrand::kMini : aa::CarBrand::kBmw;
    const bool written = writeLine(contract::kBrandFile, aa::brandName(brand));
    alog("brand: %s%s", aa::brandName(brand), written ? "" : ", not written");
    return written;
}

}  // namespace

bool writeIdriveCoding() {
    VariantServer server;
    if (!server.find()) {
        unlink(contract::kControllerFile);
        unlink(contract::kBrandFile);
        return false;
    }
    const bool keys = writeControllerCoding(server);
    const bool brand = writeBrand(server);
    return keys && brand;
}

}  // namespace app
}  // namespace evo
