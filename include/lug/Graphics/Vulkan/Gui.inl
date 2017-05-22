inline const Vulkan::API::Semaphore& Gui::getGuiSemaphore(uint32_t currentImageIndex) const {
    return _guiSemaphores[currentImageIndex];
}
