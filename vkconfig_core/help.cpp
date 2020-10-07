/*
 * Copyright (c) 2020 Valve Corporation
 * Copyright (c) 2020 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 * - Christophe Riccio <christophe@lunarg.com>
 */

#include "help.h"
#include "util.h"
#include "platform.h"

#include <QDesktopServices>
#include <QUrl>

#include <cassert>

static const char* GetOS() {
    if (VKC_PLATFORM_WINDOWS)
        return "windows";
    else if (VKC_PLATFORM_MACOS)
        return "mac";
    else if (VKC_PLATFORM_LINUX)
        return "linux";
    else {
        assert(0);  //"Unknown platform"
        return "";
    }
}

void ShowDoc(DocType doc_type) {
    switch (doc_type) {
        default: {
            assert(0);
            break;
        }
        case DOC_VKCONFIG_README: {
            QDesktopServices::openUrl(QUrl("https://vulkan.lunarg.com/doc/view/latest/windows/vkconfig.html"));
            break;
        }
        case DOC_VKCONFIG_CHANGELOG: {
            QDesktopServices::openUrl(QUrl("https://github.com/LunarG/VulkanTools/blob/master/vkconfig/CHANGELOG.md"));
            break;
        }
        case DOC_VULKAN_SPEC: {
            const std::string url = format("https://vulkan.lunarg.com/doc/view/latest/%s/1.2-extensions/vkspec.html", GetOS());
            QDesktopServices::openUrl(QUrl(url.c_str()));
            break;
        }
        case DOC_VULKAN_LAYERS: {
            const std::string url = format("https://vulkan.lunarg.com/doc/view/latest/%s/layer_configuration.html", GetOS());
            QDesktopServices::openUrl(QUrl(url.c_str()));
            break;
        }
    }
}
