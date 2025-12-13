/*
 * Web Server Implementation with full WebUI
 */

#include "web_server.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include "image_processor.h"
#include "power_manager.h"
#include "board_config.h"

#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

// External function from main.c to prevent sleep during activity
extern void app_reset_wifi_timer(void);

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

static const char *TAG = "webserver";

static httpd_handle_t s_server = NULL;
static settings_change_cb_t s_settings_cb = NULL;
static void *s_settings_ctx = NULL;
static image_change_cb_t s_image_cb = NULL;
static void *s_image_ctx = NULL;

// HTML template for the main page
static const char HTML_HEADER[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1'>"
    "<title>E1001 Photo Frame</title>"
    "<style>"
    ":root{--bg-body:#121212;--bg-card:#1e1e1e;--bg-input:#2d2d2d;--text-main:#e0e0e0;--text-muted:#a0a0a0;--primary:#3b82f6;--primary-hover:#2563eb;--danger:#ef4444;--success:#22c55e;--warning:#f59e0b;--border:#333;--radius:12px;--shadow:0 4px 6px -1px rgba(0,0,0,0.1),0 2px 4px -1px rgba(0,0,0,0.06)}"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;background:var(--bg-body);color:var(--text-main);line-height:1.6;-webkit-font-smoothing:antialiased;padding:20px}"
    ".container{max-width:800px;margin:0 auto}"
    "h1{font-size:1.75rem;font-weight:700;margin-bottom:1.5rem;color:var(--text-main);letter-spacing:-0.025em}"
    ".card{background:var(--bg-card);border-radius:var(--radius);padding:1.5rem;margin-bottom:1.5rem;border:1px solid var(--border);box-shadow:var(--shadow)}"
    ".card h2{font-size:1.1rem;font-weight:600;margin-bottom:1.25rem;color:var(--text-main);display:flex;align-items:center;gap:8px}"
    ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:1rem}"
    ".status-item{background:rgba(255,255,255,0.03);padding:1rem;border-radius:8px;border:1px solid var(--border)}"
    ".status-item .label{font-size:0.75rem;text-transform:uppercase;letter-spacing:0.05em;color:var(--text-muted);margin-bottom:0.25rem}"
    ".status-item .value{font-size:1.25rem;font-weight:600;color:var(--text-main)}"
    ".form-group{margin-bottom:1.25rem}"
    ".form-group label{display:block;margin-bottom:0.5rem;color:var(--text-muted);font-size:0.9rem}"
    ".form-group input,.form-group select{width:100%;padding:0.75rem;background:var(--bg-input);border:1px solid var(--border);border-radius:6px;color:var(--text-main);font-size:0.95rem;transition:border-color 0.2s}"
    ".form-group input:focus,.form-group select:focus{outline:none;border-color:var(--primary)}"
    ".checkbox-group{display:flex;align-items:center;gap:0.75rem;padding:0.5rem 0}"
    ".checkbox-group input{width:1.2rem;height:1.2rem;accent-color:var(--primary)}"
    ".checkbox-group label{margin:0;cursor:pointer}"
    ".btn-group{display:flex;gap:0.75rem;flex-wrap:wrap;margin-top:1.5rem}"
    "button{background:var(--primary);color:white;border:none;padding:0.75rem 1.25rem;border-radius:6px;font-weight:500;cursor:pointer;transition:all 0.2s;font-size:0.9rem;display:inline-flex;align-items:center;gap:6px}"
    "button:hover{background:var(--primary-hover);transform:translateY(-1px)}"
    "button:disabled{opacity:0.5;cursor:not-allowed;transform:none}"
    "button.danger{background:rgba(239,68,68,0.1);color:var(--danger);border:1px solid rgba(239,68,68,0.2)}"
    "button.danger:hover{background:var(--danger);color:white}"
    "button.secondary{background:var(--bg-input);color:var(--text-main);border:1px solid var(--border)}"
    "button.secondary:hover{background:rgba(255,255,255,0.1)}"
    "button.warning{background:rgba(245,158,11,0.1);color:var(--warning);border:1px solid rgba(245,158,11,0.2)}"
    "button.warning:hover{background:var(--warning);color:white}"
    ".upload-zone{border:2px dashed var(--border);border-radius:var(--radius);padding:2rem;text-align:center;cursor:pointer;transition:all 0.2s;background:rgba(255,255,255,0.01)}"
    ".upload-zone:hover,.upload-zone.drag{border-color:var(--primary);background:rgba(59,130,246,0.05)}"
    ".upload-zone p{margin:0.5rem 0;color:var(--text-muted)}"
    ".upload-zone.processing{border-color:var(--warning);background:rgba(245,158,11,0.05)}"
    ".progress{height:6px;background:var(--bg-input);border-radius:3px;margin-top:1rem;overflow:hidden;display:none}"
    ".progress-bar{height:100%;background:var(--primary);width:0;transition:width 0.2s}"
    ".upload-status{margin-top:1rem;padding:0.75rem;background:var(--bg-input);border-radius:6px;font-size:0.85rem;display:none}"
    ".upload-status.show{display:block}"
    ".upload-status .log{max-height:150px;overflow-y:auto;font-family:monospace;font-size:0.75rem;margin-top:0.5rem;padding:0.5rem;background:rgba(0,0,0,0.3);border-radius:4px}"
    ".images-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:1rem;margin-top:1rem}"
    ".image-card{background:var(--bg-input);border-radius:8px;overflow:hidden;border:1px solid var(--border);transition:transform 0.2s}"
    ".image-card:hover{transform:translateY(-2px);border-color:var(--primary)}"
    ".image-card .preview{aspect-ratio:5/3;background:#000;display:flex;align-items:center;justify-content:center;overflow:hidden}"
    ".image-card .preview img{width:100%;height:100%;object-fit:contain}"
    ".image-card .info{padding:0.75rem}"
    ".image-card .name{font-size:0.85rem;font-weight:500;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-bottom:0.25rem}"
    ".image-card .size{font-size:0.75rem;color:var(--text-muted)}"
    ".image-card .actions{padding:0 0.75rem 0.75rem;display:flex;gap:0.5rem}"
    ".image-card button{padding:0.4rem;flex:1;justify-content:center;font-size:0.8rem}"
    ".toast{position:fixed;bottom:24px;right:24px;background:var(--bg-card);color:var(--text-main);padding:1rem 1.5rem;border-radius:8px;box-shadow:0 10px 15px -3px rgba(0,0,0,0.3);border-left:4px solid var(--primary);transform:translateY(150%);transition:transform 0.3s cubic-bezier(0.4,0,0.2,1);z-index:100;max-width:90vw}"
    ".toast.show{transform:translateY(0)}"
    ".toast.error{border-left-color:var(--danger)}"
    ".toast.success{border-left-color:var(--success)}"
    ".toast.warning{border-left-color:var(--warning)}"
    ".modal{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);display:none;align-items:center;justify-content:center;z-index:200}"
    ".modal.show{display:flex}"
    ".modal-content{background:var(--bg-card);border-radius:var(--radius);padding:1.5rem;max-width:90%;max-height:90%;overflow:auto}"
    ".preview-modal img{max-width:100%;max-height:70vh;display:block;margin:0 auto}"
    "@media(max-width:600px){body{padding:10px}.container{width:100%}.card{padding:1rem}.images-grid{grid-template-columns:repeat(auto-fill,minmax(140px,1fr))}}"
    "</style></head><body>";

static const char HTML_DASHBOARD_BODY[] =
    "<div class='container'>"
    "<h1>📷 E1001 Photo Frame</h1>"

    // Status card
    "<div class='card'><h2>📊 Status</h2>"
    "<div class='status-grid'>"
    "<div class='status-item'><div class='label'>WiFi</div><div class='value' id='wifi-status'>-</div></div>"
    "<div class='status-item'><div class='label'>IP Address</div><div class='value' id='ip-addr'>-</div></div>"
    "<div class='status-item'><div class='label'>Battery</div><div class='value' id='battery'>-</div></div>"
    "<div class='status-item'><div class='label'>Images</div><div class='value' id='image-count'>-</div></div>"
    "<div class='status-item'><div class='label'>SD Card</div><div class='value' id='sd-status'>-</div></div>"
    "<div class='status-item'><div class='label'>Free Space</div><div class='value' id='free-space'>-</div></div>"
    "</div></div>"

    // Upload card with client-side processing
    "<div class='card'><h2>📤 Upload Images</h2>"
    "<div class='upload-zone' id='upload-zone'>"
    "<input type='file' id='file-input' accept='image/*,.heic,.heif' multiple>"
    "<p>📁 Click or drag images here</p>"
    "<p style='font-size:0.8em;color:var(--text-muted);margin-top:10px'>Supports: JPG, PNG, BMP, HEIC (iPhone)</p>"
    "<p style='font-size:0.75em;color:var(--text-muted)'>Images processed on device: 800x480 grayscale</p>"
    "</div>"
    "<div class='progress' id='progress'><div class='progress-bar' id='progress-bar'></div></div>"
    "<div class='upload-status' id='upload-status'>"
    "<div id='upload-summary'>Ready</div>"
    "<div class='log' id='upload-log'></div>"
    "</div>"
    "</div>"

    // Images card
    "<div class='card'><h2>🖼️ Images</h2>"
    "<div class='btn-group' style='margin-top:0;margin-bottom:1rem'>"
    "<button onclick='refreshImages()' class='secondary'>🔄 Refresh</button>"
    "<button onclick='displayNext()' class='secondary'>▶️ Next Image</button>"
    "<button onclick='deleteAllImages()' class='danger'>🗑️ Delete All</button>"
    "</div>"
    "<div class='images-grid' id='images-grid'>Loading...</div>"
    "</div>"

    // Settings card
    "<div class='card'><h2>⚙️ Settings</h2>"
    "<form id='settings-form'>"
    "<div class='form-group'>"
    "<label>Carousel Interval (seconds)</label>"
    "<input type='number' id='interval' min='10' max='86400' value='300'>"
    "</div>"
    "<div class='form-group'>"
    "<label>WiFi Auto-off Timeout (seconds)</label>"
    "<input type='number' id='wifi-timeout' min='30' max='600' value='60'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Timezone (UTC offset in hours)</label>"
    "<input type='number' id='timezone' min='-12' max='14' value='0'>"
    "</div>"
    "<div class='form-group checkbox-group'>"
    "<input type='checkbox' id='show-datetime'>"
    "<label for='show-datetime'>Show Date/Time</label>"
    "</div>"
    "<div class='form-group checkbox-group'>"
    "<input type='checkbox' id='show-temp'>"
    "<label for='show-temp'>Show Temperature</label>"
    "</div>"
    "<div class='form-group checkbox-group'>"
    "<input type='checkbox' id='show-battery'>"
    "<label for='show-battery'>Show Battery Level</label>"
    "</div>"
    "<div class='form-group checkbox-group'>"
    "<input type='checkbox' id='show-wifi'>"
    "<label for='show-wifi'>Show WiFi Status</label>"
    "</div>"
    "<div class='form-group checkbox-group'>"
    "<input type='checkbox' id='random-order'>"
    "<label for='random-order'>Random Order</label>"
    "</div>"
    "<div class='form-group checkbox-group'>"
    "<input type='checkbox' id='fit-mode'>"
    "<label for='fit-mode'>Keep Margins (Fit to Screen)</label>"
    "</div>"
    "<div class='btn-group'>"
    "<button type='submit'>💾 Save Settings</button>"
    "<button type='button' onclick='location.href=\"/wifi\"' class='secondary'>📶 Configure WiFi</button>"
    "<button type='button' onclick='restartDevice()' class='secondary'>🔄 Restart Device</button>"
    "<button type='button' onclick='formatSD()' class='warning'>💾 Format SD</button>"
    "<button type='button' onclick='factoryReset()' class='danger'>🗑️ Factory Reset</button>"
    "</div>"
    "</form></div>"

    "</div><div class='toast' id='toast'></div>"
    "<div class='modal' id='preview-modal' onclick='closePreview()'>"
    "<div class='modal-content preview-modal'><img id='preview-img' src=''></div>"
    "</div>";

static const char HTML_WIFI_BODY[] =
    "<div class='container'>"
    "<h1>📶 WiFi Configuration</h1>"
    "<div class='card'>"
    "<form id='wifi-form'>"
    "<div class='form-group'>"
    "<label>Network (SSID)</label>"
    "<input type='text' id='wifi-ssid' placeholder='Your WiFi network'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Password</label>"
    "<input type='password' id='wifi-pass' placeholder='WiFi password'>"
    "</div>"
    "<div class='btn-group'>"
    "<button type='submit'>🔗 Connect</button>"
    "<button type='button' onclick='scanNetworks()' class='secondary'>📡 Scan Networks</button>"
    "<button type='button' onclick='location.href=\"/\"' class='secondary'>🏠 Dashboard</button>"
    "</div>"
    "<div id='networks-list' style='margin-top:15px;display:flex;flex-wrap:wrap;gap:0.5rem'></div>"
    "</form></div>"
    "</div><div class='toast' id='toast'></div>";

static const char HTML_SCRIPT[] =
    "<script>"
    "const API='/api';"
    "const TARGET_W=800,TARGET_H=480;"
    "let uploadLog=[];"

    // Toast notification with different types
    "function showToast(msg,type='info'){"
    "const t=document.getElementById('toast');"
    "t.textContent=msg;t.className='toast show'+(type==='error'?' error':type==='success'?' success':type==='warning'?' warning':'');"
    "setTimeout(()=>t.className='toast',4000)}"

    // Upload log helper
    "function log(msg,isError=false){"
    "const ts=new Date().toLocaleTimeString();"
    "uploadLog.push(`[${ts}] ${msg}`);"
    "const logEl=document.getElementById('upload-log');"
    "if(logEl){logEl.innerHTML=uploadLog.slice(-20).join('<br>');logEl.scrollTop=logEl.scrollHeight}"
    "if(isError)console.error(msg);else console.log(msg)}"

    "async function fetchJSON(url,opts){try{const r=await fetch(url,opts);const d=await r.json();if(!r.ok)throw new Error(d.error||'Request failed');return d}catch(e){showToast('Error: '+e.message,'error');log('API Error: '+e.message,true);return null}}"

    "async function refreshStatus(){"
    "const d=await fetchJSON(API+'/status');"
    "if(d){"
    "document.getElementById('wifi-status').textContent=d.wifi_connected?'Connected':'Disconnected';"
    "document.getElementById('ip-addr').textContent=d.ip||'-';"
    "document.getElementById('battery').textContent=d.battery+'%';"
    "document.getElementById('image-count').textContent=d.image_count;"
    "document.getElementById('sd-status').textContent=d.sd_mounted?'Mounted':'Not Found';"
    "document.getElementById('free-space').textContent=(d.free_mb||0)+'MB'}}"

    "let imgObserver=new IntersectionObserver((entries,obs)=>{"
    "entries.forEach(entry=>{"
    "if(entry.isIntersecting){"
    "const img=entry.target;"
    "img.src=img.dataset.src;"
    "img.classList.remove('lazy');"
    "obs.unobserve(img)}})});"

    "async function refreshImages(){"
    "const d=await fetchJSON(API+'/images');"
    "const g=document.getElementById('images-grid');"
    "if(!d||!d.images||!d.images.length){g.innerHTML='<p style=\"color:var(--text-muted)\">No images uploaded yet</p>';return}"
    "g.innerHTML=d.images.map((img,i)=>"
    "'<div class=\"image-card\">"
    "<div class=\"preview\" onclick=\"previewImage(\\''+img.name+'\\')\"><img data-src=\"'+API+'/thumb/'+encodeURIComponent(img.name)+'\" src=\"data:image/svg+xml,%3Csvg xmlns=\\'http://www.w3.org/2000/svg\\' viewBox=\\'0 0 5 3\\' fill=\\'%23333\\'%3E%3Crect width=\\'5\\' height=\\'3\\'/%3E%3C/svg%3E\" class=\"lazy\" onerror=\"this.style.display=\\'none\\';this.parentNode.innerHTML=\\'<span style=padding:1rem>'+img.name.split(`.`).pop().toUpperCase()+'</span>\\'\"></div>"
    "<div class=\"info\"><div class=\"name\" title=\"'+img.name+'\">'+img.name+'</div>"
    "<div class=\"size\">'+(img.size/1024).toFixed(1)+' KB</div></div>"
    "<div class=\"actions\">"
    "<button onclick=\"displayImage('+i+')\" class=\"secondary\">📺</button>"
    "<button onclick=\"deleteImage(\\''+img.name+'\\')\" class=\"danger\">🗑️</button>"
    "</div></div>').join('');"
    "document.querySelectorAll('img.lazy').forEach(img=>imgObserver.observe(img))}"

    "function previewImage(name){"
    "document.getElementById('preview-img').src=API+'/files/'+encodeURIComponent(name);"
    "document.getElementById('preview-modal').classList.add('show')}"
    "function closePreview(){document.getElementById('preview-modal').classList.remove('show')}"

    "async function loadSettings(){"
    "const d=await fetchJSON(API+'/settings');"
    "if(d){"
    "document.getElementById('interval').value=d.carousel_interval||300;"
    "document.getElementById('wifi-timeout').value=d.wifi_timeout||60;"
    "document.getElementById('timezone').value=d.timezone||0;"
    "document.getElementById('show-datetime').checked=d.show_datetime!==false;"
    "document.getElementById('show-temp').checked=d.show_temperature!==false;"
    "document.getElementById('show-battery').checked=d.show_battery!==false;"
    "document.getElementById('show-wifi').checked=d.show_wifi!==false;"
    "document.getElementById('random-order').checked=d.random_order===true;"
    "document.getElementById('fit-mode').checked=d.fit_mode===true}}"

    "async function saveSettings(e){"
    "e.preventDefault();"
    "const data={carousel_interval:+document.getElementById('interval').value,"
    "wifi_timeout:+document.getElementById('wifi-timeout').value,"
    "timezone:+document.getElementById('timezone').value,"
    "show_datetime:document.getElementById('show-datetime').checked,"
    "show_temperature:document.getElementById('show-temp').checked,"
    "show_battery:document.getElementById('show-battery').checked,"
    "show_wifi:document.getElementById('show-wifi').checked,"
    "random_order:document.getElementById('random-order').checked,"
    "fit_mode:document.getElementById('fit-mode').checked};"
    "const r=await fetchJSON(API+'/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});"
    "if(r&&r.success)showToast('Settings saved!','success')}"

    "async function connectWifi(e){"
    "e.preventDefault();"
    "const ssid=document.getElementById('wifi-ssid').value;"
    "const pass=document.getElementById('wifi-pass').value;"
    "showToast('Connecting...','info');"
    "const r=await fetchJSON(API+'/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password:pass})});"
    "if(r&&r.success)showToast('Connected!','success');else showToast('Connection failed','error')}"

    "async function scanNetworks(){"
    "const list=document.getElementById('networks-list');"
    "if(!list)return;"
    "list.innerHTML='Scanning...';"
    "const r=await fetchJSON(API+'/wifi/scan');"
    "if(r&&r.networks){list.innerHTML=r.networks.map(n=>'<button type=\"button\" class=\"secondary\" style=\"margin:2px\" onclick=\"document.getElementById(\\'wifi-ssid\\').value=\\''+n+'\\'\">' + n + '</button>').join('')}"
    "else list.innerHTML='Scan failed'}"

    "async function deleteImage(name){"
    "if(!confirm('Delete '+name+'?'))return;"
    "const r=await fetchJSON(API+'/images/'+encodeURIComponent(name),{method:'DELETE'});"
    "if(r&&r.success){showToast('Deleted!','success');refreshImages();refreshStatus()}}"

    "async function deleteAllImages(){"
    "if(!confirm('Delete ALL images? This cannot be undone!'))return;"
    "const r=await fetchJSON(API+'/images',{method:'DELETE'});"
    "if(r&&r.success){showToast('All images deleted!','success');refreshImages();refreshStatus()}}"

    "async function displayImage(idx){"
    "await fetchJSON(API+'/display/'+idx,{method:'POST'});"
    "showToast('Displaying image...','info')}"

    "async function displayNext(){"
    "await fetchJSON(API+'/display/next',{method:'POST'});"
    "showToast('Displaying next image...','info')}"

    "async function restartDevice(){"
    "if(!confirm('Restart device?'))return;"
    "await fetchJSON(API+'/restart',{method:'POST'});"
    "showToast('Restarting...','warning');"
    "setTimeout(()=>location.reload(),5000)}"

    "async function formatSD(){"
    "if(!confirm('Format SD card? This will DELETE ALL IMAGES!'))return;"
    "if(!confirm('Are you REALLY sure? All data will be lost!'))return;"
    "showToast('Formatting SD card...','warning');"
    "const r=await fetchJSON(API+'/format',{method:'POST'});"
    "if(r&&r.success){showToast('SD card formatted!','success');refreshImages();refreshStatus()}"
    "else showToast('Format failed: '+(r?.error||'Unknown error'),'error')}"

    "async function factoryReset(){"
    "if(!confirm('Reset all settings?'))return;"
    "await fetchJSON(API+'/reset',{method:'POST'});"
    "showToast('Reset complete','success');loadSettings()}"

    // Client-side image processing - resize and convert to grayscale
    "function processImage(file){return new Promise((resolve,reject)=>{"
    "log('Processing: '+file.name+' ('+Math.round(file.size/1024)+'KB)');"
    "const img=new Image();"
    "const url=URL.createObjectURL(file);"
    "img.onload=()=>{"
    "URL.revokeObjectURL(url);"
    "log('Loaded: '+img.width+'x'+img.height);"
    // Calculate scaling to fit 800x480
    "const scale=Math.min(TARGET_W/img.width,TARGET_H/img.height);"
    "const w=Math.round(img.width*scale);"
    "const h=Math.round(img.height*scale);"
    "const canvas=document.createElement('canvas');"
    "canvas.width=TARGET_W;canvas.height=TARGET_H;"
    "const ctx=canvas.getContext('2d');"
    // Fill with white background
    "ctx.fillStyle='#FFFFFF';ctx.fillRect(0,0,TARGET_W,TARGET_H);"
    // Center the image
    "const x=Math.round((TARGET_W-w)/2);"
    "const y=Math.round((TARGET_H-h)/2);"
    "ctx.drawImage(img,x,y,w,h);"
    // Convert to grayscale for better e-ink quality
    "const imageData=ctx.getImageData(0,0,TARGET_W,TARGET_H);"
    "const data=imageData.data;"
    "for(let i=0;i<data.length;i+=4){"
    "const gray=Math.round(0.299*data[i]+0.587*data[i+1]+0.114*data[i+2]);"
    "data[i]=data[i+1]=data[i+2]=gray}"
    "ctx.putImageData(imageData,0,0);"
    "log('Processed to '+TARGET_W+'x'+TARGET_H+' grayscale');"
    // Export as JPEG (smaller than PNG)
    "canvas.toBlob(blob=>{"
    "if(blob){log('Output size: '+Math.round(blob.size/1024)+'KB');resolve(blob)}"
    "else{log('Canvas export failed',true);reject(new Error('Canvas export failed'))}"
    "},'image/jpeg',0.92)};"
    "img.onerror=()=>{URL.revokeObjectURL(url);log('Failed to load image',true);reject(new Error('Image load failed'))};"
    "img.src=url})}"

    // File upload with client-side processing
    "const zone=document.getElementById('upload-zone');"
    "if(zone){"
    "const input=document.getElementById('file-input');"
    "const progress=document.getElementById('progress');"
    "const progressBar=document.getElementById('progress-bar');"
    "const statusEl=document.getElementById('upload-status');"
    "const summaryEl=document.getElementById('upload-summary');"

    "zone.addEventListener('click',e=>{if(e.target===zone||e.target.tagName==='P')input.click()});"
    "zone.addEventListener('dragover',e=>{e.preventDefault();zone.classList.add('drag')});"
    "zone.addEventListener('dragleave',()=>zone.classList.remove('drag'));"
    "zone.addEventListener('drop',e=>{e.preventDefault();zone.classList.remove('drag');handleFiles(e.dataTransfer.files)});"
    "input.addEventListener('change',e=>handleFiles(e.target.files));"
    "}"

    "async function handleFiles(files){"
    "if(!files||!files.length)return;"
    "uploadLog=[];log('Starting upload of '+files.length+' file(s)');"
    "const zone=document.getElementById('upload-zone');"
    "const progress=document.getElementById('progress');"
    "const progressBar=document.getElementById('progress-bar');"
    "const statusEl=document.getElementById('upload-status');"
    "const summaryEl=document.getElementById('upload-summary');"
    "zone.classList.add('processing');"
    "statusEl.classList.add('show');"
    "progress.style.display='block';progressBar.style.width='0%';"
    "let done=0,success=0,failed=0;"
    "for(const file of files){"
    "summaryEl.textContent=`Processing ${done+1}/${files.length}: ${file.name}`;"
    "try{"
    // Skip non-image files
    "if(!file.type.startsWith('image/')&&!file.name.match(/\\.(heic|heif)$/i)){"
    "log('Skipping non-image: '+file.name,true);failed++;done++;continue}"
    // Process image client-side
    "let blob;"
    "try{blob=await processImage(file)}catch(e){"
    "log('Process error: '+e.message+', sending original',true);"
    "blob=file}"
    // Upload processed image
    "const fd=new FormData();"
    "const newName=file.name.replace(/\\.[^.]+$/,'.jpg');"
    "fd.append('file',blob,newName);"
    "log('Uploading: '+newName);"
    "const r=await fetch(API+'/upload',{method:'POST',body:fd});"
    "const j=await r.json();"
    "if(j.success){log('✓ Uploaded: '+j.filename);success++}"
    "else{log('✗ Upload failed: '+(j.error||'Unknown'),true);failed++}"
    "}catch(e){log('✗ Error: '+e.message,true);failed++}"
    "done++;progressBar.style.width=(done/files.length*100)+'%'}"
    "zone.classList.remove('processing');"
    "progress.style.display='none';"
    "summaryEl.textContent=`Done: ${success} uploaded, ${failed} failed`;"
    "if(success>0){showToast(success+' image(s) uploaded!','success');refreshImages();refreshStatus()}"
    "else if(failed>0)showToast('Upload failed for all files','error');"
    "document.getElementById('file-input').value=''}"

    "const settingsForm=document.getElementById('settings-form');"
    "if(settingsForm)settingsForm.addEventListener('submit', saveSettings);"
    "const wifiForm=document.getElementById('wifi-form');"
    "if(wifiForm)wifiForm.addEventListener('submit', connectWifi);"

    "refreshStatus();refreshImages();loadSettings();"
    "setInterval(refreshStatus,10000);"
    "</script></body></html>";

// API Handlers
static esp_err_t handle_root(httpd_req_t *req)
{
    // Check if we are in AP mode (captive portal)
    wifi_mgr_info_t wifi_info;
    wifi_mgr_get_info(&wifi_info);

    const char *body = HTML_DASHBOARD_BODY;
    if (wifi_info.mode == WIFI_MGR_MODE_AP) {
        body = HTML_WIFI_BODY;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, HTML_HEADER, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, body, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, HTML_SCRIPT, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t handle_wifi_ui(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, HTML_HEADER, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, HTML_WIFI_BODY, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, HTML_SCRIPT, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t handle_status(httpd_req_t *req)
{
    wifi_mgr_info_t wifi_info;
    wifi_mgr_get_info(&wifi_info);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_info.status == WIFI_MGR_STATUS_CONNECTED);
    cJSON_AddStringToObject(root, "ip", wifi_info.ip_addr[0] ? wifi_info.ip_addr : wifi_info.ap_ip_addr);
    cJSON_AddNumberToObject(root, "battery", power_get_battery_percent());
    cJSON_AddNumberToObject(root, "image_count", storage_get_image_count());
    cJSON_AddBoolToObject(root, "sd_mounted", storage_sd_mounted());
    cJSON_AddNumberToObject(root, "free_mb", storage_get_free_space() / (1024 * 1024));

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_get_images(httpd_req_t *req)
{
    image_info_t *images = malloc(MAX_IMAGES * sizeof(image_info_t));
    if (!images)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    int count = storage_get_images(images, MAX_IMAGES);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "images");

    for (int i = 0; i < count; i++)
    {
        cJSON *img = cJSON_CreateObject();
        cJSON_AddStringToObject(img, "name", images[i].filename);
        cJSON_AddNumberToObject(img, "size", images[i].size);
        cJSON_AddItemToArray(arr, img);
    }

    free(images);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_delete_all_images(httpd_req_t *req)
{
    esp_err_t ret = storage_delete_all_images();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", ret == ESP_OK);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);

    if (ret == ESP_OK && s_image_cb)
    {
        // Notify with NULL filename to indicate all deleted or refresh needed
        s_image_cb(NULL, false, s_image_ctx);
    }

    return ESP_OK;
}

static esp_err_t handle_delete_image(httpd_req_t *req)
{
    char filename[MAX_FILENAME_LEN];

    // Extract filename from URI
    const char *uri = req->uri;
    const char *name_start = strrchr(uri, '/');
    if (name_start)
    {
        name_start++;
        strncpy(filename, name_start, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';

        // URL decode
        char *dst = filename;
        for (char *src = filename; *src; src++)
        {
            if (*src == '%' && src[1] && src[2])
            {
                int val;
                sscanf(src + 1, "%2x", &val);
                *dst++ = (char)val;
                src += 2;
            }
            else
            {
                *dst++ = *src;
            }
        }
        *dst = '\0';
    }

    esp_err_t ret = storage_delete_image(filename);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", ret == ESP_OK);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);

    if (ret == ESP_OK && s_image_cb)
    {
        s_image_cb(filename, false, s_image_ctx);
    }

    return ESP_OK;
}

static esp_err_t handle_upload(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=== Upload request started ===");
    ESP_LOGI(TAG, "Content-Length: %d bytes", req->content_len);
    
    char filename[MAX_FILENAME_LEN] = "image.bin";
    char buf[1024];
    int received;
    int remaining = req->content_len;

    // Validate content length
    if (remaining <= 0) {
        ESP_LOGE(TAG, "Upload failed: No content (length=%d)", remaining);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", false);
        cJSON_AddStringToObject(err, "error", "No content received");
        char *json = cJSON_PrintUnformatted(err);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json);
        free(json);
        cJSON_Delete(err);
        return ESP_FAIL;
    }

    if (remaining > 16 * 1024 * 1024) { // 16MB limit
        ESP_LOGE(TAG, "Upload failed: File too large (%d bytes)", remaining);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", false);
        cJSON_AddStringToObject(err, "error", "File too large (max 16MB)");
        char *json = cJSON_PrintUnformatted(err);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json);
        free(json);
        cJSON_Delete(err);
        return ESP_FAIL;
    }

    // Check SD card status
    if (!storage_sd_mounted()) {
        ESP_LOGE(TAG, "Upload failed: SD card not mounted");
        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", false);
        cJSON_AddStringToObject(err, "error", "SD card not mounted");
        char *json = cJSON_PrintUnformatted(err);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json);
        free(json);
        cJSON_Delete(err);
        return ESP_FAIL;
    }

    // Allocate memory for the file data
    uint8_t *file_data = heap_caps_malloc(remaining + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!file_data)
    {
        file_data = malloc(remaining + 1);
    }
    if (!file_data)
    {
        ESP_LOGE(TAG, "Upload failed: Cannot allocate %d bytes", remaining);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", false);
        cJSON_AddStringToObject(err, "error", "Out of memory");
        char *json = cJSON_PrintUnformatted(err);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json);
        free(json);
        cJSON_Delete(err);
        return ESP_FAIL;
    }

    size_t file_size = 0;
    bool in_data = false;
    char boundary[128] = "";
    int total_received = 0;

    ESP_LOGI(TAG, "Starting to receive data...");

    while (remaining > 0)
    {
        // Reset WiFi timeout timer to prevent sleep during upload
        app_reset_wifi_timer();

        // Read into buf, leave space for null terminator
        received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf) - 1));
        if (received <= 0)
        {
            if (received == HTTPD_SOCK_ERR_TIMEOUT)
            {
                ESP_LOGW(TAG, "Receive timeout, retrying...");
                continue;
            }
            ESP_LOGE(TAG, "Receive error: %d", received);
            break;
        }
        buf[received] = '\0'; // Null terminate for string operations
        total_received += received;

        if (!in_data)
        {
            // Find boundary on first chunk
            if (boundary[0] == '\0')
            {
                char *bound_end = strstr(buf, "\r\n");
                if (bound_end)
                {
                    size_t len = bound_end - buf;
                    if (len < sizeof(boundary) - 1)
                    {
                        memcpy(boundary, buf, len);
                        boundary[len] = '\0';
                        ESP_LOGI(TAG, "Found boundary: %s", boundary);
                    }
                }
            }

            // Find filename
            char *fname = strstr(buf, "filename=\"");
            if (fname)
            {
                fname += 10;
                char *fname_end = strchr(fname, '"');
                if (fname_end && fname_end - fname < MAX_FILENAME_LEN)
                {
                    memcpy(filename, fname, fname_end - fname);
                    filename[fname_end - fname] = '\0';
                    ESP_LOGI(TAG, "Filename from header: %s", filename);
                }
            }

            // Find data start (after \r\n\r\n)
            char *data_start = strstr(buf, "\r\n\r\n");
            if (data_start)
            {
                data_start += 4;
                in_data = true;
                size_t header_len = data_start - buf;
                size_t data_len = received - header_len;
                if (data_len > 0)
                {
                    memcpy(file_data + file_size, data_start, data_len);
                    file_size += data_len;
                }
                ESP_LOGI(TAG, "Header parsed, starting data copy (%d bytes in first chunk)", (int)data_len);
            }
        }
        else
        {
            memcpy(file_data + file_size, buf, received);
            file_size += received;
        }

        remaining -= received;
        
        // Log progress every ~100KB
        if (total_received % (100 * 1024) < 1024) {
            ESP_LOGI(TAG, "Received %d bytes so far...", total_received);
        }
    }

    // Remove trailing boundary
    // The data ends with \r\n--boundary--\r\n
    // We search backwards for the boundary string in the binary data
    if (file_size > 0 && strlen(boundary) > 0)
    {
        size_t bound_len = strlen(boundary);
        size_t scan_len = (file_size > 512) ? 512 : file_size;
        uint8_t *scan_start = file_data + file_size - scan_len;

        for (size_t i = 0; i < scan_len - bound_len; i++)
        {
            if (memcmp(scan_start + i, boundary, bound_len) == 0)
            {
                // Found boundary match
                uint8_t *boundary_ptr = scan_start + i;

                // Check for preceding \r\n
                if (boundary_ptr >= file_data + 2)
                {
                    if (*(boundary_ptr - 1) == '\n' && *(boundary_ptr - 2) == '\r')
                    {
                        file_size = (boundary_ptr - 2) - file_data;
                        break;
                    }
                }
                // Fallback: just cut at boundary
                file_size = boundary_ptr - file_data;
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Upload complete: %s (%d bytes)", filename, (int)file_size);

    // Detect image format
    const char *format = img_detect_format(file_data, file_size);
    ESP_LOGI(TAG, "Detected format: %s", format);

    // Save original file
    esp_err_t ret = storage_save_image(filename, file_data, file_size);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save image: %s", esp_err_to_name(ret));
        free(file_data);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "error", "Failed to save to SD card");
        char *json = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json);
        free(json);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    free(file_data);
    ESP_LOGI(TAG, "Image saved successfully");

    // Process image (generate thumbnail and optimized bin)
    ESP_LOGI(TAG, "Starting post-processing...");
    img_process_upload(filename);
    ESP_LOGI(TAG, "Post-processing complete");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "filename", filename);
    cJSON_AddStringToObject(root, "format", format);
    cJSON_AddNumberToObject(root, "size", file_size);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);

    if (s_image_cb)
    {
        s_image_cb(filename, true, s_image_ctx);
    }

    ESP_LOGI(TAG, "=== Upload request completed ===");
    return ESP_OK;
}

static esp_err_t handle_get_settings(httpd_req_t *req)
{
    app_settings_t settings;
    storage_load_settings(&settings);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "carousel_interval", settings.carousel_interval_sec);
    cJSON_AddBoolToObject(root, "show_datetime", settings.show_datetime);
    cJSON_AddBoolToObject(root, "show_temperature", settings.show_temperature);
    cJSON_AddBoolToObject(root, "show_battery", settings.show_battery);
    cJSON_AddBoolToObject(root, "show_wifi", settings.show_wifi);
    cJSON_AddBoolToObject(root, "random_order", settings.random_order);
    cJSON_AddBoolToObject(root, "fit_mode", settings.fit_mode);

    char *json = cJSON_PrintUnformatted(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_set_settings(httpd_req_t *req)
{
    char buf[512];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    app_settings_t settings;
    storage_load_settings(&settings);

    cJSON *val;
    if ((val = cJSON_GetObjectItem(json, "carousel_interval")))
        settings.carousel_interval_sec = val->valueint;
    if ((val = cJSON_GetObjectItem(json, "wifi_timeout")))
        settings.wifi_timeout_sec = val->valueint;
    if ((val = cJSON_GetObjectItem(json, "timezone")))
        settings.timezone_offset = val->valueint;
    if ((val = cJSON_GetObjectItem(json, "show_datetime")))
        settings.show_datetime = cJSON_IsTrue(val);
    if ((val = cJSON_GetObjectItem(json, "show_temperature")))
        settings.show_temperature = cJSON_IsTrue(val);
    if ((val = cJSON_GetObjectItem(json, "show_battery")))
        settings.show_battery = cJSON_IsTrue(val);
    if ((val = cJSON_GetObjectItem(json, "show_wifi")))
        settings.show_wifi = cJSON_IsTrue(val);
    if ((val = cJSON_GetObjectItem(json, "random_order")))
        settings.random_order = cJSON_IsTrue(val);
    if ((val = cJSON_GetObjectItem(json, "fit_mode")))
        settings.fit_mode = cJSON_IsTrue(val);

    cJSON_Delete(json);

    esp_err_t ret = storage_save_settings(&settings);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", ret == ESP_OK);

    char *resp = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);

    free(resp);
    cJSON_Delete(root);

    if (ret == ESP_OK && s_settings_cb)
    {
        s_settings_cb(&settings, s_settings_ctx);
    }

    return ESP_OK;
}

static esp_err_t handle_wifi_connect(httpd_req_t *req)
{
    char buf[256];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *pass = cJSON_GetObjectItem(json, "password");

    esp_err_t ret = ESP_FAIL;
    if (ssid && cJSON_IsString(ssid))
    {
        ret = wifi_mgr_connect(ssid->valuestring,
                               pass && cJSON_IsString(pass) ? pass->valuestring : "",
                               true);
    }

    cJSON_Delete(json);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", ret == ESP_OK);

    char *resp = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);

    free(resp);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_wifi_scan(httpd_req_t *req)
{
    char networks[20][33];
    int count = wifi_mgr_scan(networks, 20);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");

    for (int i = 0; i < count; i++)
    {
        cJSON_AddItemToArray(arr, cJSON_CreateString(networks[i]));
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_display(httpd_req_t *req)
{
    // Extract index from URI
    const char *idx_str = strrchr(req->uri, '/');
    int index = 0;

    if (idx_str)
    {
        idx_str++;
        if (strcmp(idx_str, "next") == 0)
        {
            index = -1; // Special value for next
        }
        else
        {
            index = atoi(idx_str);
        }
    }

    // The actual display will be handled by carousel module
    // We just store the request
    if (index == -1)
    {
        // Trigger next image via carousel
        extern void carousel_next(void);
        carousel_next();
    }
    else
    {
        extern void carousel_show_index(int idx);
        carousel_show_index(index);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static void restart_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    vTaskDelete(NULL);
}

static esp_err_t handle_restart(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);

    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t handle_reset(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Factory reset requested");
    app_settings_t settings;
    storage_reset_settings(&settings);
    storage_save_settings(&settings);
    wifi_mgr_clear_credentials();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);

    // return esp_err_t type
    return ESP_OK;
}

static esp_err_t handle_format_sd(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=== SD Card format requested ===");
    
    cJSON *root = cJSON_CreateObject();
    
    if (!storage_sd_mounted()) {
        ESP_LOGE(TAG, "Format failed: SD card not mounted");
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "error", "SD card not mounted");
    } else {
        // Delete all images first (safer than actual format)
        ESP_LOGI(TAG, "Deleting all images...");
        esp_err_t ret = storage_delete_all_images();
        
        if (ret == ESP_OK) {
            // Re-create images directory
            storage_create_images_dir();
            ESP_LOGI(TAG, "SD card cleaned successfully");
            cJSON_AddBoolToObject(root, "success", true);
            cJSON_AddStringToObject(root, "message", "All images deleted");
            
            // Get updated free space
            uint64_t free_space = storage_get_free_space();
            cJSON_AddNumberToObject(root, "free_mb", (double)(free_space / (1024 * 1024)));
        } else {
            ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
            cJSON_AddBoolToObject(root, "success", false);
            cJSON_AddStringToObject(root, "error", "Failed to delete images");
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "=== Format request completed ===");
    return ESP_OK;
}

static esp_err_t handle_get_file(httpd_req_t *req)
{
    const char *filename_ptr = strrchr(req->uri, '/');
    if (!filename_ptr)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }
    filename_ptr++; // Skip '/'

    char filename[MAX_FILENAME_LEN];
    strncpy(filename, filename_ptr, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';

    // URL decode
    char *dst = filename;
    for (char *src = filename; *src; src++)
    {
        if (*src == '%' && src[1] && src[2])
        {
            int val;
            sscanf(src + 1, "%2x", &val);
            *dst++ = (char)val;
            src += 2;
        }
        else
        {
            *dst++ = *src;
        }
    }
    *dst = '\0';

    // Determine MIME type
    const char *ext = strrchr(filename, '.');
    const char *mime = "application/octet-stream";
    if (ext)
    {
        if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
            mime = "image/jpeg";
        else if (strcasecmp(ext, ".png") == 0)
            mime = "image/png";
        else if (strcasecmp(ext, ".bmp") == 0)
            mime = "image/bmp";
    }

    uint8_t *data = NULL;
    size_t size = 0;
    if (storage_load_image(filename, &data, &size) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, mime);
    httpd_resp_send(req, (const char *)data, size);
    free(data);
    return ESP_OK;
}

static esp_err_t handle_get_thumbnail(httpd_req_t *req)
{
    const char *filename_ptr = strrchr(req->uri, '/');
    if (!filename_ptr)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }
    filename_ptr++; // Skip '/'

    char filename[MAX_FILENAME_LEN];
    strncpy(filename, filename_ptr, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';

    // URL decode
    char *dst = filename;
    for (char *src = filename; *src; src++)
    {
        if (*src == '%' && src[1] && src[2])
        {
            int val;
            sscanf(src + 1, "%2x", &val);
            *dst++ = (char)val;
            src += 2;
        }
        else
        {
            *dst++ = *src;
        }
    }
    *dst = '\0';

    // Append .thumb
    char thumb_path[MAX_FILENAME_LEN + 8];
    snprintf(thumb_path, sizeof(thumb_path), "%s.thumb", filename);

    // Check if file exists to avoid error log
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", IMAGES_DIR, thumb_path);
    struct stat st;
    if (stat(full_path, &st) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Thumbnail not found");
        return ESP_FAIL;
    }

    uint8_t *data = NULL;
    size_t size = 0;
    // Try to load thumbnail
    if (storage_load_image(thumb_path, &data, &size) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Thumbnail not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_send(req, (const char *)data, size);
    free(data);
    return ESP_OK;
}

static esp_err_t handle_captive_portal(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Captive portal redirect for URI: %s", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t webserver_start(void) {
    if (s_server) {
        return ESP_OK;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 24;    // Increased to accommodate all handlers including format
    config.stack_size = 32768;       // Increased stack size for image processing
    config.lru_purge_enable = false; // Disable LRU purge for stability
    config.max_open_sockets = 4;     // Conservative socket limit
    config.recv_wait_timeout = 30;   // Increase receive timeout for large uploads
    config.send_wait_timeout = 30;   // Increase send timeout
    
    ESP_LOGI(TAG, "Starting web server with config: handlers=%d, stack=%d, sockets=%d",
             config.max_uri_handlers, config.stack_size, config.max_open_sockets);
    
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register handlers
    static const httpd_uri_t handlers[] = {
        { .uri = "/", .method = HTTP_GET, .handler = handle_root },
        { .uri = "/wifi", .method = HTTP_GET, .handler = handle_wifi_ui },
        { .uri = "/api/status", .method = HTTP_GET, .handler = handle_status },
        { .uri = "/api/images", .method = HTTP_GET, .handler = handle_get_images },
        { .uri = "/api/images", .method = HTTP_DELETE, .handler = handle_delete_all_images },
        { .uri = "/api/images/*", .method = HTTP_DELETE, .handler = handle_delete_image },
        { .uri = "/api/upload", .method = HTTP_POST, .handler = handle_upload },
        { .uri = "/api/settings", .method = HTTP_GET, .handler = handle_get_settings },
        { .uri = "/api/settings", .method = HTTP_POST, .handler = handle_set_settings },
        { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = handle_wifi_connect },
        { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = handle_wifi_scan },
        { .uri = "/api/display/*", .method = HTTP_POST, .handler = handle_display },
        { .uri = "/api/restart", .method = HTTP_POST, .handler = handle_restart },
        { .uri = "/api/reset", .method = HTTP_POST, .handler = handle_reset },
        { .uri = "/api/format", .method = HTTP_POST, .handler = handle_format_sd },
        { .uri = "/api/files/*", .method = HTTP_GET, .handler = handle_get_file },
        { .uri = "/api/thumb/*", .method = HTTP_GET, .handler = handle_get_thumbnail },
        // Captive Portal catch-all
        { .uri = "*", .method = HTTP_GET, .handler = handle_captive_portal }
    };

    for (int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); i++) {
        httpd_register_uri_handler(s_server, &handlers[i]);
    }
    
    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

void webserver_stop(void)
{
    if (s_server)
    {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

bool webserver_is_running(void)
{
    return s_server != NULL;
}

void webserver_set_settings_callback(settings_change_cb_t cb, void *ctx)
{
    s_settings_cb = cb;
    s_settings_ctx = ctx;
}

void webserver_set_image_callback(image_change_cb_t cb, void *ctx)
{
    s_image_cb = cb;
    s_image_ctx = ctx;
}

void webserver_notify_refresh(void)
{
    // Could implement WebSocket notification here
}
