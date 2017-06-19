inline Pipeline::Id Pipeline::getId() const {
    return _id;
}

inline const API::GraphicsPipeline& Pipeline::getPipelineAPI() {
    return _pipeline;
}
