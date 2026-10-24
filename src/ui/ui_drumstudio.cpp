#include "app.h"
#include "winutil.h"
#include "../core/util.h"
#include "../audio/drum_banks.h"
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
namespace rdj {
using json=nlohmann::json;
std::string App::captureDrumBank()const {
    json sources=json::array();
    for(const auto& slot:drumSamples_) {
        if(slot.factory>=0)sources.push_back({{"factory",slot.factory}});
        else if(!slot.path.empty())sources.push_back({{"file",toUtf8(slot.path)}});
        else sources.push_back({{"synth",true}});
    }
    auto instrument=json::parse(instrToJson());
    return json{{"sources",sources},{"voices",instrument["ritmos"]["sonidos"]}}.dump();
}
std::string App::factoryDrumBank(int kit)const {
    if(kit<0||kit>=int(kFactoryDrumKits.size()))return {};
    auto data=json::parse(captureDrumBank());
    const float decay[8]={350,250,300,90,700,600,80,2200};
    for(int v=0;v<8;++v){
        data["sources"][v]={{"factory",kit}};
        auto& x=data["voices"][drumVoiceId(v)];
        x={{"nivel",v==3?.65f:.8f},{"afinacion",.5f},{"caida",.5f},{"silencio",false},{"solo",false},
            {"ataqueMs",.5f},{"holdMs",0.f},{"decayMs",decay[v]},{"pitchEnv",0.f},{"pitchEnvMs",50.f},
            {"cutHz",18000.f},{"drive",0.f},{"pan",0.f},{"choke",(v==3||v==4)?1:0}};
    }
    return data.dump();
}
void App::startDrumBank(const std::string& data,const std::string& name) {
    if(drumLoad_.valid()){notify("Espera a que termine la carga del kit.",true);return;}
    auto j=json::parse(data,nullptr,false);
    if(!j.is_object()||!j.contains("sources")||!j["sources"].is_array()||j["sources"].size()!=8||!j.contains("voices")||!j["voices"].is_object()) {notify("El banco de sonidos no es válido.",true);return;}
    for(int v=0;v<8;++v) {
        auto& source=j["sources"][v];if(!source.is_object()){notify("Fuente no válida.",true);return;}
        if(source.contains("factory")&&source["factory"].is_number_integer()){
            const int kit=source["factory"].get<int>();if(kit<0||kit>=int(kFactoryDrumKits.size())){notify("Kit no reconocido.",true);return;}source={{"factory",kit}};
        }else if(source.contains("file")&&source["file"].is_string()&&!source["file"].get<std::string>().empty())source={{"file",source["file"]}};
        else if(source.contains("synth")&&source["synth"].is_boolean()&&source["synth"].get<bool>())source={{"synth",true}};
        else{notify("Fuente de sample no válida.",true);return;}
        const auto id=drumVoiceId(v);if(!j["voices"].contains(id)||!j["voices"][id].is_object()){notify("Faltan voces en el banco.",true);return;}
        const auto& controls=j["voices"][id];
        for(const char* key:{"nivel","afinacion","caida","ataqueMs","holdMs","decayMs","pitchEnv","pitchEnvMs","cutHz","drive","pan"})
            if(controls.contains(key)&&!controls[key].is_number()){notify("Valor numérico inválido en el banco.",true);return;}
        for(const char* key:{"silencio","solo"})if(controls.contains(key)&&!controls[key].is_boolean()){notify("Valor booleano inválido en el banco.",true);return;}
        if(controls.contains("choke")&&!controls["choke"].is_number_integer()){notify("Grupo de corte inválido.",true);return;}
    }
    const std::string checkedData=j.dump();
    drumInitialized_=true;drumPending_=checkedData;
    const auto current=json::parse(captureDrumBank());
    std::array<TrackPtr,8> cached{};for(int v=0;v<8;++v)cached[v]=drumSamples_[v].track;
    drumLoad_=std::async(std::launch::async,[j,current,cached,checkedData,name](){
        DrumBankResult result;result.data=checkedData;result.name=name;
        try {
            for(int v=0;v<8;++v){
                const auto& source=j["sources"][v];if(!source.is_object()){result.error="Fuente de sonido inválida.";return result;}
                if(source==current["sources"][v]&&cached[v]){result.samples[v]=cached[v];continue;}
                if(source.contains("factory")&&source["factory"].is_number_integer()) {
                    result.samples[v]=makeFactoryDrum(source["factory"].get<int>(),v);
                    if(!result.samples[v]){result.error="Kit de fábrica desconocido.";return result;}
                }else if(source.contains("file")&&source["file"].is_string()) {
                    std::string error;const auto path=fromUtf8(source["file"].get<std::string>());
                    auto track=decodeDrumSample(path,error);
                    if(!track||track->frames<2||track->lengthSec()>10){result.error="No se pudo cargar un sample de hasta 10 segundos: "+toUtf8(path);return result;}
                    result.samples[v]=track;
                }else if(!source.contains("synth")||!source["synth"].is_boolean()||!source["synth"].get<bool>()) {
                    result.error="Fuente de sonido no admitida.";return result;
                }
            }
        }catch(const std::exception& e){result.error=std::string("No se pudo preparar el kit: ")+e.what();}
        return result;
    });
}
void App::drumLibraryTick() {
    if(!drumInitialized_){drumInitialized_=true;auto data=json::parse(factoryDrumBank(0));data["voices"]=json::parse(instrToJson())["ritmos"]["sonidos"];startDrumBank(data.dump(),kFactoryDrumKits[0].name);}
    for(size_t i=0;i<drumRetired_.size();) {
        const auto& r=drumRetired_[i];
        if(engine_.rack.instr.status.drumSampleGen[r.voice].load(std::memory_order_acquire)>=r.gen)drumRetired_.erase(drumRetired_.begin()+i);else ++i;
    }
    if(!drumLoad_.valid()||drumLoad_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    auto loaded=drumLoad_.get();
    if(!loaded.error.empty()){notify(loaded.error,true);return;}
    auto data=json::parse(loaded.data);
    for(int v=0;v<8;++v){
        if(drumSamples_[v].track==loaded.samples[v])continue;
        const uint32_t gen=++drumGen_;
        Command c{Cmd::DrumSample};c.i=v;c.track=loaded.samples[v].get();c.gen=gen;
        if(!engine_.send(c)){notify("Cola de audio ocupada: vuelve a seleccionar el kit.",true);return;}
        auto& old=drumSamples_[v];if(old.track)drumRetired_.push_back({v,gen,old.track});
        old.track=loaded.samples[v];old.factory=-1;old.path.clear();
        const auto& source=data["sources"][v];
        if(source.contains("factory"))old.factory=source["factory"].get<int>();
        if(source.contains("file"))old.path=fromUtf8(source["file"].get<std::string>());
    }
    try {
        // Preserve the selected pattern, swing, probability and transport.
        auto current=json::parse(instrToJson());current["ritmos"]["sonidos"]=data["voices"];
        instrFromJson(current.dump());
    }catch(const json::exception&){notify("Los controles del banco no son válidos.",true);return;}
    drumPending_.clear();
    drumKitName_=loaded.name;
    saveRack();notify("Kit preparado: "+loaded.name);
}
std::string App::drumLibraryToJson()const {
    json banks=json::array();for(const auto& b:drumUserBanks_)banks.push_back({{"name",b.name},{"data",json::parse(b.data)}});
    return json{{"current",json::parse(drumPending_.empty()?captureDrumBank():drumPending_)},{"name",drumKitName_},{"banks",banks}}.dump();
}
void App::drumLibraryFromJson(const std::string& text) {
    auto j=json::parse(text,nullptr,false);if(!j.is_object())return;
    if(j.contains("banks")&&j["banks"].is_array())for(const auto& b:j["banks"]){
        if(drumUserBanks_.size()>=128)break;
        if(b.is_object()&&b.contains("name")&&b["name"].is_string()&&b.contains("data")&&b["data"].is_object())drumUserBanks_.push_back({b["name"].get<std::string>().substr(0,60),b["data"].dump()});
    }
    if(j.contains("current")&&j["current"].is_object())startDrumBank(j["current"].dump(),j.contains("name")&&j["name"].is_string()?j["name"].get<std::string>():"Restaurado");
}
void App::drawDrumStudio() {
    if(!drumStudioOpen_)return;
    ImGui::SetNextWindowSize(ImVec2(820,760),ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("CAJA PRO · Kits y sonidos",&drumStudioOpen_)){ImGui::End();return;}
    auto& p=engine_.rack.instr.params;const bool busy=drumLoad_.valid();
    ImGui::TextWrapped("24 kits / 192 sonidos generados con síntesis propia. Puedes combinar kits, añadir samples y guardar tus bancos. No cambia el patrón al cargar un kit.");
    ImGui::Text("%s%s",drumKitName_.empty()?(busy?"Preparando…":"Síntesis anterior"):drumKitName_.c_str(),busy?" · cargando…":"");
    ImGui::BeginDisabled(busy);
    if(ImGui::BeginCombo("Kits de fábrica",drumKitName_.c_str())){
        for(int k=0;k<int(kFactoryDrumKits.size());++k)if(ImGui::Selectable(kFactoryDrumKits[k].name))startDrumBank(factoryDrumBank(k),kFactoryDrumKits[k].name);
        ImGui::EndCombo();
    }
    if(ImGui::BeginCombo("Mis bancos","Elegir banco guardado…")){
        for(size_t i=0;i<drumUserBanks_.size();++i){
            ImGui::PushID(int(i));if(ImGui::Selectable(drumUserBanks_[i].name.c_str()))startDrumBank(drumUserBanks_[i].data,drumUserBanks_[i].name);
            bool erase=false;if(ImGui::BeginPopupContextItem("bankMenu")){erase=ImGui::MenuItem("Eliminar banco de la lista (conserva los samples)");ImGui::EndPopup();}
            ImGui::PopID();if(erase){drumUserBanks_.erase(drumUserBanks_.begin()+i);saveRack();break;}
        }
        ImGui::EndCombo();
    }
    ImGui::InputText("Nombre del banco",drumBankName_,sizeof drumBankName_);
    if(ImGui::Button("Guardar banco personal")&&drumBankName_[0]){
        const std::string data=captureDrumBank();bool found=false;
        for(auto& b:drumUserBanks_)if(b.name==drumBankName_){b.data=data;found=true;break;}
        if(!found&&drumUserBanks_.size()<128){drumUserBanks_.push_back({drumBankName_,data});found=true;}
        else if(!found)notify("Se ha alcanzado el límite de 128 bancos personales.",true);
        if(found){drumKitName_=drumBankName_;if(saveRack())notify("Banco guardado.");}
    }
    ImGui::SameLine();
    if(ImGui::Button("Exportar banco JSON…")){
        const auto path=win::pickSaveFile(hwnd_,L"Banco-bateria.json",L"Banco JSON",L"*.json",L"json");
        if(!path.empty()) {std::ofstream f(std::filesystem::path(path),std::ios::binary);f<<json{{"name",drumBankName_[0]?drumBankName_:"Mi banco"},{"data",json::parse(captureDrumBank())}}.dump(2);if(!f)notify("No se pudo exportar el banco.",true);else notify("Banco exportado. Los samples externos se referencian por su ruta.");}
    }
    ImGui::SameLine();if(ImGui::Button("Importar banco JSON…")){
        const auto path=win::pickFileToOpen(hwnd_,L"Banco JSON",L"*.json");
        if(!path.empty()){
            std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
            if(ec||size>1024*1024)notify("No se pudo leer el banco (máximo 1 MB).",true);
            else{std::ifstream f(std::filesystem::path(path),std::ios::binary);auto j=json::parse(f,nullptr,false);
                if(j.is_object()&&j.contains("data")&&j["data"].is_object())startDrumBank(j["data"].dump(),j.contains("name")&&j["name"].is_string()?j["name"].get<std::string>():"Importado");else notify("Formato de banco no reconocido.",true);}
        }
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    const float voiceWidth=std::max(70.f,(ImGui::GetContentRegionAvail().x-3*ImGui::GetStyle().ItemSpacing.x)/4);
    for(int v=0;v<8;++v){if(v%4)ImGui::SameLine();ImGui::PushID(v);if(ImGui::Selectable(drumVoiceName(v),drumEditVoice_==v,0,ImVec2(voiceWidth,24)))drumEditVoice_=v;ImGui::PopID();}
    const int v=drumEditVoice_;ImGui::Text("VOZ: %s",drumVoiceName(v));
    ImGui::BeginDisabled(busy);
    if(ImGui::Button("Escuchar golpe")){Command c{Cmd::DrumHit};c.i=v;c.d=1;engine_.send(c);}
    ImGui::SetItemTooltip("Dispara por el bus de instrumentos hacia la mezcla. Clic derecho: asignar MIDI.");
    if(ImGui::BeginPopupContextItem("drumLearn")){if(ImGui::MenuItem("Aprender botón MIDI")){midi_.startLearn(MA_DrumHit1+v,0);notify("Pulsa el pad o botón MIDI…");}ImGui::EndPopup();}
    ImGui::SameLine();if(ImGui::Button("Cargar sample propio…")){
        const auto files=win::pickAudioFiles(hwnd_,false);if(!files.empty()){auto data=json::parse(captureDrumBank());data["sources"][v]={{"file",toUtf8(files[0])}};startDrumBank(data.dump(),"Personalizado");}
    }
    if(ImGui::BeginCombo("Sonido de otro kit",drumSamples_[v].factory>=0?kFactoryDrumKits[drumSamples_[v].factory].name:drumSamples_[v].path.empty()?"Síntesis anterior":"Sample propio")){
        for(int k=0;k<int(kFactoryDrumKits.size());++k)if(ImGui::Selectable(kFactoryDrumKits[k].name)){auto data=json::parse(captureDrumBank());data["sources"][v]={{"factory",k}};startDrumBank(data.dump(),"Kit combinado");}
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    bool mute=p.dMute[v].load(),solo=p.dSolo[v].load();if(ImGui::Checkbox("Mute",&mute))p.dMute[v]=mute;ImGui::SameLine();if(ImGui::Checkbox("Solo",&solo))p.dSolo[v]=solo;
    auto slider=[](const char* label,std::atomic<float>& a,float lo,float hi,const char* format){float value=a.load();if(ImGui::SliderFloat(label,&value,lo,hi,format))a=value;};
    slider("Nivel",p.dLevel[v],0,1,"%.2f");float semis=(p.dTune[v].load()-.5f)*24;if(ImGui::SliderFloat("Afinación",&semis,-12,12,"%+.1f semitonos"))p.dTune[v]=semis/24+.5f;
    ImGui::BeginDisabled(!drumSamples_[v].track);
    if(ImGui::BeginTable("drumControls",2,ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableNextColumn();ImGui::TextUnformatted("ENVOLVENTE DE VOLUMEN");
        slider("Ataque",p.dAttackMs[v],.2f,200,"%.1f ms");slider("Hold",p.dHoldMs[v],0,500,"%.1f ms");slider("Decay",p.dDecayMs[v],5,5000,"%.1f ms");
        ImGui::TableNextColumn();ImGui::TextUnformatted("ENVOLVENTE DE TONO");
        slider("Profundidad",p.dPitchEnv[v],-24,24,"%+.1f semitonos");slider("Caída del tono",p.dPitchEnvMs[v],2,1000,"%.1f ms");
        ImGui::TextUnformatted("COLOR");slider("Filtro paso bajo",p.dCutHz[v],100,20000,"%.0f Hz");slider("Saturación",p.dDrive[v],0,1,"%.2f");slider("Panorama",p.dPan[v],-1,1,"%.2f");
        int choke=p.dChoke[v].load();if(ImGui::SliderInt("Grupo de corte",&choke,0,4))p.dChoke[v]=choke;
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Las envolventes y el color se toman al disparar el siguiente golpe. El sample termina si se acaba su audio; Decay no alarga el archivo. El control Caída del panel anterior escala este Decay. Los bancos JSON guardan referencias, no incluyen los samples externos.");
    ImGui::End();
}
} // namespace rdj
