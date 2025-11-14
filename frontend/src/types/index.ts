export interface UserInfo {
  username: string;
  role: string;
}

export interface TrdpConfigSummary {
  id: number;
  name: string;
  validation_status: string;
  created_at: string;
}

export interface TrdpConfigDetail extends TrdpConfigSummary {
  user_id: number;
  xml: string;
}

export interface NetworkConfig {
  interface_name: string;
  local_ip: string;
  multicast_groups: string[];
  pd_port: number;
  md_port: number;
}

export interface PdMessage {
  id: number;
  name: string;
  payload_hex: string;
  updated_at: string;
}

export interface MdMessage {
  id: number;
  subject: string;
  payload_hex: string;
  direction: 'incoming' | 'outgoing';
  timestamp: string;
}
