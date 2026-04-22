#include "connector.h"
#include "config.h"
#include "connector_qwen.h"
#include "connector_openai.h"
#include "connector_bedrock.h"

std::unique_ptr<Connector> make_connector(const Config& cfg) {
    switch (cfg.connector_type) {
        case ConnectorType::OpenAiJson:
            return std::make_unique<OpenAiConnector>(cfg);
        case ConnectorType::Bedrock:
            return std::make_unique<BedrockConnector>(cfg);
        case ConnectorType::QwenXml:
        default:
            return std::make_unique<QwenConnector>(cfg);
    }
}
