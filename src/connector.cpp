#include "connector.h"
#include "config.h"
#include "connector_openai.h"
#include "connector_bedrock.h"

std::unique_ptr<Connector> make_connector(const Config& cfg) {
    switch (cfg.connector_type) {
        case ConnectorType::Bedrock:
            return std::make_unique<BedrockConnector>(cfg);
        case ConnectorType::OpenAiJson:
        default:
            return std::make_unique<OpenAiConnector>(cfg);
    }
}
