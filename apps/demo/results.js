export const labels = { name: '姓名', sex: '性别', ethnicity: '民族', birthDate: '出生日期', address: '住址', idNumber: '身份证号码', authority: '签发机关', validity: '有效期限', nationality: '国籍', licenseNumber: '证号', firstIssueDate: '初次领证日期', vehicleClass: '准驾车型', plateNumber: '号牌号码', vehicleType: '车辆类型', owner: '所有人', useCharacter: '使用性质', model: '品牌型号', vin: '车辆识别代号', engineNumber: '发动机号码', registerDate: '注册日期', issueDate: '发证日期' };
const schemas = {
  portrait: ['name', 'sex', 'ethnicity', 'birthDate', 'address', 'idNumber'],
  emblem: ['authority', 'validity'],
  driver_license: ['name', 'sex', 'nationality', 'address', 'birthDate', 'licenseNumber', 'firstIssueDate', 'vehicleClass', 'validity'],
  vehicle_license: ['plateNumber', 'vehicleType', 'owner', 'address', 'useCharacter', 'model', 'vin', 'engineNumber', 'registerDate', 'issueDate']
};
export const fieldKeys = result => schemas[result.documentType === 'id_card' ? result.side : result.documentType];
export const needsReview = warning => !warning.endsWith(':CAPTION_REMOVED');
const messages = {
  MISSING: '未识别，请重新拍摄或核对原图', LOW_CONFIDENCE: '文字不够清晰，请核对', REVIEW_REQUIRED: '请核对原图',
  CHECKSUM_INVALID: '号码校验未通过', FORMAT_INVALID: '格式未通过校验', FIELD_INVALID: '内容未通过校验',
  DATE_INVALID: '日期无效', DATE_FORMAT_UNRECOGNIZED: '日期格式需要核对', AMBIGUOUS_CHARACTER: '含易混淆字符，请核对',
  OCR_DISAGREEMENT: '多次识别内容不一致，请核对', CAPTION_REMOVED: '已清理字段标题杂字',
  DOCUMENT_TYPE_MISMATCH: '图片与所选证件类型或页面不符，请切换', DOCUMENT_TYPE_UNCONFIRMED: '无法确认所选证件，请核对图片与类型',
  MULTIPLE_DOCUMENTS: '检测到多个证件区域，请只保留一张证件', UNSUPPORTED_PAGE: '请选择证件主页',
  BORDER_INCOMPLETE: '文字靠近图片边缘，请确认拍摄完整', LOW_SHARPNESS: '图片可能模糊，请对焦后重新拍摄'
};
export function warningText(warning) {
  const [field, code] = warning.includes(':') ? warning.split(':') : [null, warning];
  return `${field ? (labels[field] || field) + '：' : ''}${messages[code] || '识别结果需要核对'}`;
}
export function suggestedDocument(result) {
  if (!result.warnings.includes('DOCUMENT_TYPE_MISMATCH')) return;
  const text = result.lines.map(line => line.text).join('');
  if (text.includes('驾驶证')) return { documentType: 'driver_license', side: 'main', label: '驾驶证主页' };
  if (text.includes('行驶证')) return { documentType: 'vehicle_license', side: 'main', label: '行驶证主页' };
  if (text.includes('身份号码')) return { documentType: 'id_card', side: 'portrait', label: '身份证人像面' };
  if (text.includes('居民身份证')) return { documentType: 'id_card', side: 'emblem', label: '身份证国徽面' };
}
