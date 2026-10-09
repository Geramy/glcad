#include "test.hpp"

#include "ai/client.hpp"
#include "ai/discovery.hpp"
#include "ai/types.hpp"

using namespace gai;

TEST(ai_optional_stays_off_the_wire) {
    SpeechRequest r;
    r.model = "kokoro";
    r.input = "hello";
    const std::string j = r.to_json();
    CHECK(j.find("\"speed\"") == std::string::npos);
    CHECK(j.find("\"voice\":\"af_heart\"") != std::string::npos);
    r.speed = 1.25;
    CHECK(r.to_json().find("\"speed\":1.25") != std::string::npos);
}

TEST(ai_chat_request_wire_shape) {
    ChatRequest r;
    r.model = "qwen";
    r.messages.push_back(ChatMessage::text("system", "be brief"));
    r.messages.push_back(ChatMessage::text("user", "hi"));
    r.temperature = 0.2;
    const std::string j = r.to_json();
    CHECK(j.find("\"model\":\"qwen\"") != std::string::npos);
    CHECK(j.find("\"role\":\"system\"") != std::string::npos);
    CHECK(j.find("\"content\":\"be brief\"") != std::string::npos);
    CHECK(j.find("\"stream\":false") != std::string::npos);
    CHECK(j.find("\"temperature\"") != std::string::npos);
    CHECK(j.find("\"top_p\"") == std::string::npos);
}

TEST(ai_vision_message_uses_content_parts) {
    ChatMessage m;
    m.role = "user";
    m.parts.push_back(ContentPart::of_text("what is this?"));
    m.parts.push_back(ContentPart::of_image("data:image/png;base64,AAAA"));
    const std::string j = m.to_value().dump();
    CHECK(j.find("\"type\":\"text\"") != std::string::npos);
    CHECK(j.find("\"type\":\"image_url\"") != std::string::npos);
    CHECK(j.find("\"url\":\"data:image/png;base64,AAAA\"") != std::string::npos);
    ChatMessage back;
    CHECK(back.parse(j));
    CHECK(back.parts.size() == 2);
    CHECK(back.parts[1].image_url.has());
}

TEST(ai_chat_response_parses) {
    const char* wire = R"({"id":"c1","model":"qwen","object":"chat.completion",
        "choices":[{"index":0,"message":{"role":"assistant","content":"42"},
        "finish_reason":"stop"}],"usage":{"prompt_tokens":10,"completion_tokens":1,
        "total_tokens":11}})";
    ChatResponse r;
    CHECK(r.parse(wire));
    CHECK(r.text() && *r.text() == "42");
    CHECK(r.usage.has() && r.usage.get().total_tokens.has() &&
          r.usage.get().total_tokens.get() == 11);
}

TEST(ai_stream_delta_parses) {
    ChatDelta d;
    CHECK(d.parse(R"({"choices":[{"delta":{"content":"He"},"finish_reason":null}]})"));
    CHECK(d.choices.size() == 1 && d.choices[0].delta.content.has());
    CHECK(d.choices[0].delta.content.get() == "He");
    CHECK(!d.choices[0].finish_reason.has());
}

TEST(ai_type_mismatch_fails_the_parse) {
    ChatResponse r;
    CHECK(!r.parse(R"({"choices":"not an array"})"));
    CHECK(!r.parse("not json at all"));
    CHECK(!r.parse(R"({"choices":[{"index":"zero"}]})"));
}

TEST(ai_models_and_image_wire) {
    ModelList l;
    CHECK(l.parse(R"({"object":"list","data":[{"id":"qwen","owned_by":"me"},{"id":"kokoro"}]})"));
    CHECK(l.data.size() == 2 && l.data[0].id == "qwen");
    CHECK(l.data[0].owned_by.has() && !l.data[1].owned_by.has());

    ImageRequest ir;
    ir.model = "sd";
    ir.prompt = "a lighthouse";
    ir.width = 512;
    const std::string j = ir.to_json();
    CHECK(j.find("\"width\":512") != std::string::npos);
    CHECK(j.find("\"height\"") == std::string::npos);
    CHECK(j.find("\"response_format\":\"b64_json\"") != std::string::npos);

    ImageResponse resp;
    CHECK(resp.parse(R"({"created":1,"data":[{"b64_json":"QUJD"}]})"));
    CHECK(resp.data.size() == 1 && resp.data[0].b64_json.has());
}

TEST(ai_beacon_parses_and_url_resolves) {
    Beacon b;
    CHECK(b.parse(R"({"service":"lemonade","hostname":"studio","url":"http://0.0.0.0:8000"})"));
    CHECK(b.service == "lemonade" && b.hostname == "studio");
    Beacon other;
    CHECK(other.parse(R"({"service":"printer"})"));
    CHECK(other.service != "lemonade");
}

TEST(ai_client_url_parsing) {
    auto c = Client::from_url("http://192.168.1.20:8000");
    CHECK(c && c->host() == "192.168.1.20" && c->port() == 8000);
    c = Client::from_url("http://studio.local:13305/api/v1");
    CHECK(c && c->host() == "studio.local" && c->port() == 13305);
    c = Client::from_url("10.0.0.5");
    CHECK(c && c->host() == "10.0.0.5" && c->port() == 8000);
    CHECK(!Client::from_url(""));
    CHECK(!Client::from_url("http://host:notaport"));

    c = Client::from_url("https://api.example.ai/");
    CHECK(c && c->secure() && c->port() == 443 && c->host() == "api.example.ai");
    CHECK_MSG(c->url() == "https://api.example.ai", "rendered '%s'", c->url().c_str());
    CHECK_MSG(c->api_prefix() == "/v1", "hosted prefix was '%s'", c->api_prefix().c_str());
    CHECK(Client::from_url("10.0.0.5")->api_prefix() == "/api/v1");
    CHECK(Client::from_url("https://api.example.ai/api/v1")->api_prefix() == "/api/v1");
    CHECK(Client::from_url("https://api.example.ai/api/v1")->url() ==
          "https://api.example.ai/api/v1");

    c = Client::from_url("https://api.example.ai/v1");
    CHECK(c && c->api_prefix() == "/v1");
    CHECK_MSG(c->url() == "https://api.example.ai", "rendered '%s'", c->url().c_str());
    c = Client::from_url("https://api.example.ai:8443/v1/");
    CHECK(c && c->port() == 8443 && c->api_prefix() == "/v1");
    CHECK_MSG(c->url() == "https://api.example.ai:8443", "rendered '%s'", c->url().c_str());
    c = Client::from_url("https://api.example.ai/openai/v1");
    CHECK(c && c->api_prefix() == "/openai/v1");
    CHECK(c->url() == "https://api.example.ai/openai/v1");

    CHECK(Client::from_url("http://192.168.1.20:8000")->url() ==
          "http://192.168.1.20:8000");
    CHECK(Client::from_url("10.0.0.5")->url() == "http://10.0.0.5:8000");
    CHECK(!Client::from_url("10.0.0.5")->secure());
    CHECK(Client::from_url("http://studio.local:13305/api/v1")->url() ==
          "http://studio.local:13305");

    for (const char* a : {"https://api.example.ai", "https://api.example.ai/api/v1",
                          "https://api.example.ai/openai/v1",
                          "http://192.168.1.20:8000", "http://10.0.0.5:8000"}) {
        const auto one = Client::from_url(a);
        CHECK(one.has_value());
        const auto two = Client::from_url(one->url());
        CHECK_MSG(two && two->url() == one->url(), "%s did not round-trip", a);
    }
}

TEST(ai_discovery_starts_and_stops) {
    Discovery d;
    const bool started = d.start();
    if (started) {
        CHECK(d.running());
        (void)d.snapshot();
        d.stop();
        CHECK(!d.running());
    }
}

TEST(ai_tool_definition_wire_shape) {
    Json params = Json::object();
    params["type"] = "object";
    params["properties"]["from"]["type"] = "string";
    params["required"] = Json::array({"from"});
    const auto d = ToolDefinition::make("draw_line", "Draw a line between two points", params);
    const std::string j = d.to_json();
    CHECK(j.find("\"type\":\"function\"") != std::string::npos);
    CHECK(j.find("\"name\":\"draw_line\"") != std::string::npos);
    CHECK(j.find("\"required\":[\"from\"]") != std::string::npos);

    ChatRequest r;
    r.model = "qwen";
    r.tools.emplace().push_back(d);
    r.tool_choice = Json("auto");
    const std::string rj = r.to_json();
    CHECK(rj.find("\"tools\":[{") != std::string::npos);
    CHECK(rj.find("\"tool_choice\":\"auto\"") != std::string::npos);
}

TEST(ai_tool_calls_round_trip_through_history) {
    ChatResponse resp;
    CHECK(resp.parse(R"({"choices":[{"index":0,"message":{"role":"assistant",
        "content":null,"tool_calls":[{"id":"call_1","type":"function",
        "function":{"name":"measure","arguments":"{\"a\":\"0,0\",\"b\":\"3,4\"}"}}]},
        "finish_reason":"tool_calls"}]})"));
    const auto* calls = resp.calls();
    CHECK(calls && calls->size() == 1);
    CHECK((*calls)[0].name() == "measure");
    CHECK((*calls)[0].arguments().find("3,4") != std::string::npos);

    const ChatMessage again = ChatMessage::assistant_calls(*calls);
    const std::string aj = again.to_json();
    CHECK(aj.find("\"tool_calls\":[{") != std::string::npos);
    CHECK(aj.find("\"id\":\"call_1\"") != std::string::npos);
    const ChatMessage reply = ChatMessage::tool_result("call_1", "5.0 m");
    const std::string rj = reply.to_json();
    CHECK(rj.find("\"role\":\"tool\"") != std::string::npos);
    CHECK(rj.find("\"tool_call_id\":\"call_1\"") != std::string::npos);
    CHECK(rj.find("\"content\":\"5.0 m\"") != std::string::npos);
}

TEST(ai_streamed_tool_calls_assemble_from_fragments) {
    ToolCallAssembler asm_;
    const char* frames[] = {
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_9",
            "function":{"name":"draw_line","arguments":""}}]}}]})",
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,
            "function":{"arguments":"{\"from\":\"0,0\","}}]}}]})",
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,
            "function":{"arguments":"\"to\":\"3,2\"}"}}]}}]})",
    };
    for (const char* f : frames) {
        ChatDelta d;
        CHECK(d.parse(f));
        for (const auto& ch : d.choices) asm_.feed(ch.delta);
    }
    CHECK(asm_.any());
    const auto calls = asm_.take();
    CHECK(calls.size() == 1);
    CHECK(calls[0].id && calls[0].id.get() == "call_9");
    CHECK(calls[0].name() == "draw_line");
    CHECK_MSG(calls[0].arguments() == "{\"from\":\"0,0\",\"to\":\"3,2\"}",
              "assembled '%s'", calls[0].arguments().c_str());
}

TEST(ai_optional_null_and_absent_are_different) {
    ChatDeltaChoice c;
    CHECK(c.parse(R"({"delta":{},"finish_reason":null})"));
    CHECK(c.finish_reason.exists() && c.finish_reason.is_null() && !c.finish_reason);
    CHECK(c.parse(R"({"delta":{}})"));
    CHECK(!c.finish_reason.exists());
    c.finish_reason = "stop";
    CHECK(c.finish_reason && c.to_json().find("\"finish_reason\":\"stop\"") != std::string::npos);
}

TEST(ai_model_labels_say_what_a_model_can_do) {
    ModelList l;
    CHECK(l.parse(R"({"data":[
        {"id":"LFM2.5-8B","labels":["custom","chat","tool-calling"]},
        {"id":"Qwen3.8-27B","labels":["chat","vision","tool-calling","mtp"],"max_context_window":262144},
        {"id":"Whisper-Large-v3-Turbo","labels":["transcription","realtime-transcription","hot"]},
        {"id":"kokoro-v1","labels":["tts"]},
        {"id":"unlabelled-model"}]})"));
    CHECK(l.data.size() == 5);
    CHECK(l.data[0].chats() && l.data[0].calls_tools() && !l.data[0].sees());
    CHECK(l.data[1].sees() && l.data[1].max_context_window.has() &&
          l.data[1].max_context_window.get() == 262144);
    CHECK(l.data[2].transcribes() && !l.data[2].chats());
    CHECK(l.data[3].speaks());
    CHECK(!l.data[4].labelled());
}

TEST(ai_json_parse_caps_the_depth_the_server_chooses) {
    std::string deep;
    for (int i = 0; i < 5000; ++i) deep += '[';
    for (int i = 0; i < 5000; ++i) deep += ']';
    CHECK(!gai::parse_json(deep).has_value());

    std::string sane;
    for (int i = 0; i < 100; ++i) sane += "[";
    sane += "1";
    for (int i = 0; i < 100; ++i) sane += "]";
    CHECK(gai::parse_json(sane).has_value());
    const auto v = gai::parse_json(R"({"a":{"b":[1,2,{"c":"d"}]}})");
    CHECK(v && (*v)["a"]["b"][2]["c"] == "d");
    CHECK(!gai::parse_json("{broken").has_value());
}

TEST(ai_model_roles_read_every_server_vocabulary) {
    ModelList hosted;
    CHECK(hosted.parse(R"({"object":"list","data":[
      {"id":"Qwen3.6-35B-A3B-MTP-GGUF","modality":"text",
       "labels":["text","vision","tool-calling","mtp"]},
      {"id":"Flux-2-Klein-9B-GGUF","modality":"image",
       "labels":["image","generation","edit"]},
      {"id":"kokoro-v1","modality":"tts","labels":["tts","speech"]},
      {"id":"Moonshine-Medium-Streaming","modality":"stt",
       "labels":["stt","transcription","audio","realtime-transcription"]},
      {"id":"Qwen3-Embedding-0.6B-GGUF","modality":"embedding",
       "labels":["text","embeddings"]}]})"));
    CHECK(hosted.data.size() == 5);
    const auto& chat = hosted.data[0];
    const auto& img = hosted.data[1];
    const auto& tts = hosted.data[2];
    const auto& stt = hosted.data[3];
    const auto& emb = hosted.data[4];
    CHECK_MSG(chat.chats(), "the chat model was not offered for completion");
    CHECK(chat.calls_tools() && chat.sees());
    CHECK_MSG(img.makes_images(), "the image model was not offered");
    CHECK(tts.speaks() && stt.transcribes() && emb.embeds());
    CHECK_MSG(!emb.chats(), "the embedding model was offered for completion");
    CHECK(!tts.chats() && !stt.chats() && !img.chats());
    CHECK(!chat.embeds() && !chat.speaks() && !chat.transcribes());

    ModelList lemonade;
    CHECK(lemonade.parse(R"({"data":[
      {"id":"Qwen3-8B","labels":["chat","tool-calling"]},
      {"id":"nomic-embed","labels":["embeddings"]},
      {"id":"kokoro","labels":["tts"]}]})"));
    CHECK(lemonade.data[0].chats() && !lemonade.data[0].embeds());
    CHECK(lemonade.data[1].embeds() && !lemonade.data[1].chats());
    CHECK(lemonade.data[2].speaks() && !lemonade.data[2].chats());
}

TEST(ai_a_saved_model_pick_is_checked_against_the_server) {
    ModelList list;
    CHECK(list.parse(R"({"data":[
      {"id":"Qwen3.6-35B-A3B-MTP-GGUF","modality":"text","labels":["text"]},
      {"id":"kokoro-v1","modality":"tts","labels":["tts"]}]})"));
    CHECK(gai::has_model(list.data, "Qwen3.6-35B-A3B-MTP-GGUF"));
    CHECK(gai::has_model(list.data, "Qwen3.6"));
    CHECK(gai::has_model(list.data, "kokoro-v1"));
    CHECK_MSG(!gai::has_model(list.data, "Qwen3.8-27B-FP8-vLLM"),
              "a model this server does not serve was treated as present");
    CHECK(!gai::has_model(list.data, "whisper-large"));
    CHECK(!gai::has_model(list.data, ""));
}
